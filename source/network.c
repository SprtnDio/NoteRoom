#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "network.h"
#include "secrets.h"
#include "drawing.h"
#include "ui.h"

int mqtt_sock = -1;
u64 last_ping_time = 0;
u64 last_reconnect_time = 0;
u64 lastBanSyncRequest = 0;
bool sendInProgress = false;
bool banRequestSent = false;

char admin_token[32];
char admin_reset_topic[128];
char admin_unban_topic[128];
char admin_ban_topic[128];
char admin_kick_topic[128];
char admin_list_req_topic[128];
char admin_list_res_topic[128];
char admin_status_topic[128];
char admin_announce_topic[128];
char admin_banlist_topic[128];

static u8 recv_buf[MAX_PAYLOAD_SIZE];
static int recv_len = 0;

// ====================================================================
// NEU: MQTT‑basierte Zeitsynchronisation (ersetzt NTP)
// ====================================================================
bool mqtt_sync_time() {
    if (game->ntpSyncInProgress) return false;
    game->ntpSyncInProgress = true;

    char time_req_topic[128];
    snprintf(time_req_topic, sizeof(time_req_topic), "%s/TimeRequest", g_base_topic);
    mqtt_publish(time_req_topic, game->clientID, false);

    u64 start = osGetTime();
    while (osGetTime() - start < 5000) {
        mqtt_poll();                      
        if (game->trustedTimeValid) {
            game->ntpSyncInProgress = false;
            return true;
        }
        svcSleepThread(10000000);         
    }

    game->ntpSyncInProgress = false;
    return false;
}
// ====================================================================

int mqtt_encode_length(u8* buf, int length) {
    int len = 0;
    do {
        u8 encodedByte = length % 128;
        length = length / 128;
        if (length > 0) encodedByte |= 128;
        buf[len++] = encodedByte;
    } while (length > 0);
    return len;
}

static int send_all(int sock, const void *buf, size_t len, int flags) {
    const u8 *ptr = (const u8*)buf;
    size_t remaining = len;
    while (remaining > 0) {
        int sent = send(sock, ptr, remaining, flags);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(sock, &wfds);
                struct timeval tv = {1, 0};
                int ret = select(sock + 1, NULL, &wfds, NULL, &tv);
                if (ret > 0) continue;
                else return -1;
            }
            return -1;
        }
        ptr += sent;
        remaining -= sent;
    }
    return len;
}

void mqtt_connect() {
    updateStatus("Connecting...");
    if (game) game->needsRedrawTop = true;

    struct hostent* host = gethostbyname(g_mqtt_broker);
    if (!host || host->h_addr_list[0] == NULL) {
        updateStatus("DNS Error!");
        if (game) game->connectionFailed = true;
        return;
    }

    mqtt_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (mqtt_sock < 0) {
        updateStatus("Socket Error!");
        if (game) game->connectionFailed = true;
        return;
    }

    int flags = fcntl(mqtt_sock, F_GETFL, 0);
    fcntl(mqtt_sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(g_mqtt_port);
    server.sin_addr.s_addr = *((unsigned long*)host->h_addr_list[0]);

    int res = connect(mqtt_sock, (struct sockaddr *)&server, sizeof(server));
    if (res < 0) {
        if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN || errno == EALREADY) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(mqtt_sock, &wfds);
            struct timeval tv;
            tv.tv_sec = 5; 
            tv.tv_usec = 0;
            int ret = select(mqtt_sock + 1, NULL, &wfds, NULL, &tv);
            if (ret <= 0) {
                close(mqtt_sock);
                mqtt_sock = -1;
                updateStatus("Connect Timeout!");
                if (game) game->connectionFailed = true;
                return;
            }
        } else {
            close(mqtt_sock);
            mqtt_sock = -1;
            updateStatus("Connect Failed!");
            if (game) game->connectionFailed = true;
            return;
        }
    }

    fcntl(mqtt_sock, F_SETFL, flags & ~O_NONBLOCK);

    u8 packet[256]; 
    int p = 0;
    packet[p++] = 0x10;
    
    int client_len = strlen(game->clientID);
    int user_len = strlen(g_mqtt_user);
    int pass_len = strlen(g_mqtt_pass);

    int rem_len = 10 + 2 + client_len;
    if (user_len > 0) rem_len += 2 + user_len;
    if (pass_len > 0) rem_len += 2 + pass_len;

    p += mqtt_encode_length(&packet[p], rem_len);
    
    packet[p++] = 0x00; packet[p++] = 0x04;
    packet[p++] = 'M'; packet[p++] = 'Q'; packet[p++] = 'T'; packet[p++] = 'T';
    packet[p++] = 0x04; 
    
    u8 conn_flags = 0x02;
    if (user_len > 0) conn_flags |= 0x80;
    if (pass_len > 0) conn_flags |= 0x40;
    packet[p++] = conn_flags; 
    
    packet[p++] = 0x00; packet[p++] = 0x3C;
    
    packet[p++] = (client_len >> 8) & 0xFF;
    packet[p++] = client_len & 0xFF;
    memcpy(&packet[p], game->clientID, client_len);
    p += client_len;

    if (user_len > 0) {
        packet[p++] = (user_len >> 8) & 0xFF;
        packet[p++] = user_len & 0xFF;
        memcpy(&packet[p], g_mqtt_user, user_len);
        p += user_len;
    }

    if (pass_len > 0) {
        packet[p++] = (pass_len >> 8) & 0xFF;
        packet[p++] = pass_len & 0xFF;
        memcpy(&packet[p], g_mqtt_pass, pass_len);
        p += pass_len;
    }

    int sent = send_all(mqtt_sock, packet, p, 0);
    if (sent < 0) {
        close(mqtt_sock);
        mqtt_sock = -1;
        updateStatus("Send Error!");
        if (game) game->connectionFailed = true;
        return;
    }

    fcntl(mqtt_sock, F_SETFL, flags | O_NONBLOCK);

    updateStatus("Connected!");
    last_ping_time = osGetTime();
    if (game) game->connectionFailed = false;
    
    if (mqtt_sock >= 0) {
        char topic_sub[64];
        snprintf(topic_sub, sizeof(topic_sub), "%s/Heartbeat/#", g_base_topic);
        mqtt_subscribe(topic_sub);
        
        mqtt_subscribe(admin_banlist_topic);
        
        mqtt_subscribe(admin_reset_topic);
        mqtt_subscribe(admin_unban_topic);
        mqtt_subscribe(admin_ban_topic);
        mqtt_subscribe(admin_kick_topic);
        mqtt_subscribe(admin_list_req_topic);
        mqtt_subscribe(admin_status_topic);
        mqtt_subscribe(admin_announce_topic);
        
        if (game->inChat) {
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/Review/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
            mqtt_subscribe(topic);
            
            char topic_vote[64];
            snprintf(topic_vote, sizeof(topic_vote), "%s/Vote/+/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
            mqtt_subscribe(topic_vote);
        }
        
        lastBanSyncRequest = osGetTime() + 2000;
        banRequestSent = false;
    }
}

void mqtt_subscribe(const char* topic) {
    if (mqtt_sock < 0) return;
    u8 packet[256];
    int p = 0;
    packet[p++] = 0x82;
    int rem_len = 2 + 2 + strlen(topic) + 1;
    p += mqtt_encode_length(&packet[p], rem_len);
    packet[p++] = 0x00; packet[p++] = 0x01;
    packet[p++] = 0x00; packet[p++] = strlen(topic);
    memcpy(&packet[p], topic, strlen(topic));
    p += strlen(topic);
    packet[p++] = 0x00;
    send_all(mqtt_sock, packet, p, 0);
}

void mqtt_unsubscribe(const char* topic) {
    if (mqtt_sock < 0) return;
    u8 packet[256];
    int p = 0;
    packet[p++] = 0xA2;
    int rem_len = 2 + 2 + strlen(topic);
    p += mqtt_encode_length(&packet[p], rem_len);
    packet[p++] = 0x00; packet[p++] = 0x01;
    packet[p++] = 0x00; packet[p++] = strlen(topic);
    memcpy(&packet[p], topic, strlen(topic));
    p += strlen(topic);
    send_all(mqtt_sock, packet, p, 0);
}

void mqtt_publish(const char* topic, const char* payload, bool retain) {
    if (mqtt_sock < 0) return;
    int payload_len = strlen(payload);
    int rem_len = 2 + strlen(topic) + payload_len;

    u8* packet = (u8*)malloc(rem_len + 16);
    if (!packet) {
        updateStatus("Memory Error!");
        return;
    }

    int p = 0;
    packet[p++] = retain ? 0x31 : 0x30;
    p += mqtt_encode_length(&packet[p], rem_len);
    packet[p++] = 0x00; packet[p++] = strlen(topic);
    memcpy(&packet[p], topic, strlen(topic));
    p += strlen(topic);
    memcpy(&packet[p], payload, payload_len);
    p += payload_len;

    int res = send_all(mqtt_sock, packet, p, 0);
    if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(mqtt_sock);
        mqtt_sock = -1;
        if (game) {
            game->connectionFailed = true;
            game->needsRedrawTop = true;
            game->needsRedrawBottom = true;
        }
    }
    free(packet);
}

void mqtt_ping() {
    if (mqtt_sock < 0) return;
    u64 now = osGetTime();
    if (now - last_ping_time > 15000) {
        u8 packet[2] = {0xC0, 0x00};
        int res = send_all(mqtt_sock, packet, 2, 0);
        if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            close(mqtt_sock);
            mqtt_sock = -1;
            if (game) {
                game->connectionFailed = true;
                game->needsRedrawTop = true;
                game->needsRedrawBottom = true;
            }
        } else {
            last_ping_time = now;
        }
    }
}

static int decode_remaining_length(const u8 *buf, int max_len, int *length) {
    int len = 0;
    int multiplier = 1;
    *length = 0;
    do {
        if (len >= max_len) return -1;
        u8 byte = buf[len++];
        *length += (byte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128*128*128) return -1;
        if (!(byte & 128)) break;
    } while (1);
    return len;
}

void mqtt_poll() {
    if (mqtt_sock < 0) {
        if (game && game->connectionFailed) return;
        u64 now = osGetTime();
        if (now - last_reconnect_time > 5000 || last_reconnect_time == 0) {
            last_reconnect_time = now;
            updateStatus("Reconnecting...");
            mqtt_connect();
        }
        return;
    }

    int bytes = recv(mqtt_sock, recv_buf + recv_len, MAX_PAYLOAD_SIZE - recv_len, 0);
    if (bytes > 0) {
        recv_len += bytes;
        if (game) game->lastNetworkActivity = osGetTime();
    } else if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(mqtt_sock);
        mqtt_sock = -1;
        recv_len = 0;
        if (game) {
            game->connectionFailed = true;
            game->needsRedrawTop = true;
            game->needsRedrawBottom = true;
        }
        return;
    }

    int i = 0;
    while (i < recv_len) {
        if (i + 1 > recv_len) break;
        u8 packet_type = recv_buf[i] >> 4;
        int remaining_len = 0;
        int rem_len_bytes = decode_remaining_length(recv_buf + i + 1, recv_len - i - 1, &remaining_len);
        if (rem_len_bytes < 0) {
            recv_len = 0;
            return;
        }
        int total_packet_len = 1 + rem_len_bytes + remaining_len;
        if (i + total_packet_len > recv_len) break;

        if (packet_type == 3) {
            int pos = i + 1 + rem_len_bytes;
            if (pos + 2 > recv_len) break;
            int topic_len = (recv_buf[pos] << 8) | recv_buf[pos+1];
            pos += 2;
            if (pos + topic_len > recv_len) break;
            char topic_str[128];
            if (topic_len < 127) {
                memcpy(topic_str, recv_buf + pos, topic_len);
                topic_str[topic_len] = '\0';
            } else {
                topic_str[0] = '\0';
            }
            pos += topic_len;

            int payload_len = remaining_len - (2 + topic_len);
            if (payload_len >= 0 && payload_len < MAX_PAYLOAD_SIZE) {
                char* payload = (char*)malloc(payload_len + 1);
                if (payload) {
                    if (payload_len > 0) {
                        memcpy(payload, recv_buf + pos, payload_len);
                    }
                    payload[payload_len] = '\0';

                    char expected_hb[64];
                    snprintf(expected_hb, sizeof(expected_hb), "%s/Heartbeat/C", g_base_topic);
                    char expected_vote[64];
                    snprintf(expected_vote, sizeof(expected_vote), "%s/Vote/", g_base_topic);

                    // NEU: TimeResponse empfangen
                    char time_res_topic[128];
                    snprintf(time_res_topic, sizeof(time_res_topic), "%s/ServerTime", g_base_topic);
                    if (strcmp(topic_str, time_res_topic) == 0) {
                        u64 receivedTs = 0;
                        if (sscanf(payload, "%llu", &receivedTs) == 1 && receivedTs > 1700000000ULL) {
                            game->trustedUnixTime = receivedTs;
                            game->trustedTick = getMonotonicTick();
                            game->trustedTimeValid = true;
                            saveTrustedTime();
                            updateStatus("Time synced via MQTT");
                        }
                    }
                    else if (strcmp(topic_str, admin_banlist_topic) == 0) {
                        char* timestamp_str = strtok(payload, "|");
                        if (timestamp_str) {
                            u64 receivedTs = 0;
                            if (sscanf(timestamp_str, "%llu", &receivedTs) == 1) {
                                if (receivedTs >= lastBanChangeTime) {
                                    bannedCount = 0;
                                    char* token = strtok(NULL, "|");
                                    while (token && bannedCount < 100) {
                                        char bMac[MAC_BUFFER_SIZE] = {0};
                                        char bName[16] = {0};
                                        long long bTime = 0;
                                        if (sscanf(token, "%19[^,],%15[^,],%lld", bMac, bName, &bTime) >= 1) {
                                            snprintf(bannedUsers[bannedCount].mac, sizeof(bannedUsers[0].mac), "%s", bMac);
                                            snprintf(bannedUsers[bannedCount].name, sizeof(bannedUsers[0].name), "%s", bName);
                                            bannedUsers[bannedCount].banEnd = (time_t)bTime;
                                            bannedCount++;
                                        }
                                        token = strtok(NULL, "|");
                                    }
                                    lastBanChangeTime = receivedTs;
                                    saveBannedList();
                                    
                                    if (isBanned(game->macAddress)) {
                                        if (game->inChat) {
                                            game->inChat = false;
                                            game->appState = STATE_MAIN_MENU;
                                            char hb_topic[64];
                                            snprintf(hb_topic, sizeof(hb_topic), "%s/Heartbeat/C%d/S%d",
                                                     g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                                            char leave_payload[64];
                                            snprintf(leave_payload, sizeof(leave_payload), "!%s", game->clientID);
                                            mqtt_publish(hb_topic, leave_payload, false);
                                        }
                                        char timeStr[16];
                                        getBanRemainingTime(timeStr, sizeof(timeStr), game->macAddress);
                                        char msg[64];
                                        snprintf(msg, sizeof(msg), "BANNED: %s", timeStr);
                                        updateStatus(msg);
                                    } else {
                                        updateStatus("Banlist synced from server!");
                                    }
                                }
                            }
                        }
                        if (game) game->needsRedrawTop = true;
                    }
                    else if (strncmp(topic_str, g_base_topic, strlen(g_base_topic)) == 0) {
                        if (strcmp(topic_str, admin_reset_topic) == 0 ||
                            strcmp(topic_str, admin_unban_topic) == 0 ||
                            strcmp(topic_str, admin_ban_topic) == 0 ||
                            strcmp(topic_str, admin_kick_topic) == 0 ||
                            strcmp(topic_str, admin_list_req_topic) == 0 ||
                            strcmp(topic_str, admin_status_topic) == 0 ||
                            strcmp(topic_str, admin_announce_topic) == 0) {
                            handle_admin_command(topic_str, payload);
                        }
                        else if (strncmp(topic_str, expected_vote, strlen(expected_vote)) == 0) {
                            char action[16];
                            int c, s;
                            if (sscanf(topic_str + strlen(expected_vote), "%15[^/]/C%d/S%d", action, &c, &s) == 3) {
                                if (c == game->selectedCategoryIdx && s == game->selectedSubIdx) {
                                    if (strcmp(action, "Start") == 0) {
                                        char tMac[MAC_BUFFER_SIZE]={0}, tName[16]={0}, iMac[MAC_BUFFER_SIZE]={0}, iName[16]={0};
                                        char* p1 = strchr(payload, '|');
                                        if (p1) {
                                            int lenMac = p1 - payload;
                                            if(lenMac>19) lenMac=19;
                                            snprintf(tMac, sizeof(tMac), "%.*s", lenMac, payload);
                                            char* p2 = strchr(p1 + 1, '|');
                                            if (p2) {
                                                int lenName = p2 - (p1 + 1);
                                                if(lenName>15) lenName=15;
                                                snprintf(tName, sizeof(tName), "%.*s", lenName, p1 + 1);
                                                char* p3 = strchr(p2 + 1, '|');
                                                if (p3) {
                                                    int lenIMac = p3 - (p2 + 1);
                                                    if(lenIMac>19) lenIMac=19;
                                                    snprintf(iMac, sizeof(iMac), "%.*s", lenIMac, p2 + 1);
                                                    snprintf(iName, sizeof(iName), "%s", p3 + 1);
                                                }
                                            }
                                        }
                                        if (strlen(tMac) > 0 && !game->voteActive) {
                                            game->voteActive = true;
                                            snprintf(game->voteTargetMac, sizeof(game->voteTargetMac), "%s", tMac);
                                            snprintf(game->voteTargetName, sizeof(game->voteTargetName), "%s", tName);
                                            snprintf(game->voteInitiatorMac, sizeof(game->voteInitiatorMac), "%s", iMac);
                                            snprintf(game->voteInitiatorName, sizeof(game->voteInitiatorName), "%s", iName);
                                            game->voteEndTime = osGetTime() + 30000;
                                            game->voteYes = 0;
                                            game->voteNo = 0;
                                            game->votedCount = 0;
                                            game->iHaveVoted = false;
                                            game->needsRedrawTop = true;
                                        }
                                    } else if (strcmp(action, "Cast") == 0) {
                                        if (game->voteActive) {
                                            char vMac[MAC_BUFFER_SIZE] = {0};
                                            int vVal = 0;
                                            char* p1 = strchr(payload, '|');
                                            if (p1) {
                                                int len = p1 - payload;
                                                if(len>19) len=19;
                                                snprintf(vMac, sizeof(vMac), "%.*s", len, payload);
                                                vVal = atoi(p1 + 1);
                                                
                                                bool already = false;
                                                for (int v = 0; v < game->votedCount; v++) {
                                                    if (strcmp(game->votedMacs[v], vMac) == 0) { already = true; break; }
                                                }
                                                if (!already && game->votedCount < LOBBY_MAX_USERS) {
                                                    snprintf(game->votedMacs[game->votedCount++], sizeof(game->votedMacs[0]), "%s", vMac);
                                                    if (vVal == 1) game->voteYes++; else game->voteNo++;
                                                    game->needsRedrawTop = true;
                                                }
                                            }
                                        }
                                    }
                                    else if (strcmp(action, "Result") == 0) {
                                        char targetMac[MAC_BUFFER_SIZE];
                                        snprintf(targetMac, sizeof(targetMac), "%s", payload);
                                        if (!isBanned(targetMac)) {
                                            time_t banEnd = getTrustedTime() + BAN_DURATION_SECONDS;
                                            if (addBanEntry(targetMac, "VoteBanned", banEnd)) {
                                                updateStatus("User was banned for 48 hours!");
                                            }
                                        }
                                        if (strcmp(targetMac, game->macAddress) == 0) {
                                            game->inChat = false;
                                            game->appState = STATE_MAIN_MENU;
                                            updateStatus("You were Vote-Banned for 48 hours!");
                                        } else {
                                            for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                                                if(strcmp(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].mac, targetMac) == 0) {
                                                    game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].lastSeen = 0;
                                                }
                                            }
                                        }
                                        game->voteActive = false;
                                        game->needsRedrawTop = true;
                                        game->needsRedrawBottom = true;
                                    }
                                }
                            }
                        }
                        else if (strncmp(topic_str, expected_hb, strlen(expected_hb)) == 0) {
                            bool isRetained = (recv_buf[i] & 0x1) != 0;
                            if (!isRetained) {
                                int c, s;
                                char format_str[64];
                                snprintf(format_str, sizeof(format_str), "%s/Heartbeat/C%%d/S%%d", g_base_topic);
                                if (sscanf(topic_str, format_str, &c, &s) == 2) {
                                    if (c >= 0 && c < CATEGORY_COUNT && s >= 0 && s < MAX_SUB_ROOMS) {
                                        time_t ts = getTrustedTime();
                                        game->roomActivity[c][s] = ts;
                                        if (payload[0] == '!') {
                                            for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                                                if(strcmp(game->activeUsers[c][s][u].clientID, payload + 1) == 0) {
                                                    game->activeUsers[c][s][u].lastSeen = 0;
                                                    break;
                                                }
                                            }
                                        } else {
                                            char p_cid[32] = {0};
                                            char p_name[16] = {0};
                                            char p_mac[MAC_BUFFER_SIZE] = {0};
                                            char* p1 = strchr(payload, '|');
                                            if (p1) {
                                                int clen = p1 - payload;
                                                if (clen > 31) clen = 31;
                                                snprintf(p_cid, sizeof(p_cid), "%.*s", clen, payload);
                                                char* p2 = strchr(p1 + 1, '|');
                                                if (p2) {
                                                    int nlen = p2 - (p1 + 1);
                                                    if (nlen > 15) nlen = 15;
                                                    snprintf(p_name, sizeof(p_name), "%.*s", nlen, p1 + 1);
                                                    snprintf(p_mac, sizeof(p_mac), "%s", p2 + 1);
                                                } else {
                                                    snprintf(p_name, sizeof(p_name), "%s", p1 + 1);
                                                    snprintf(p_mac, sizeof(p_mac), "000000000000000");
                                                }
                                            } else {
                                                snprintf(p_cid, sizeof(p_cid), "%s", payload);
                                                snprintf(p_name, sizeof(p_name), "Unknown");
                                                snprintf(p_mac, sizeof(p_mac), "000000000000000");
                                            }
                                            if (!isBanned(p_mac)) {
                                                bool found = false;
                                                int oldestIdx = 0;
                                                time_t oldestTime = ts;
                                                for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                                                    if(strcmp(game->activeUsers[c][s][u].clientID, p_cid) == 0) {
                                                        game->activeUsers[c][s][u].lastSeen = ts;
                                                        snprintf(game->activeUsers[c][s][u].name, sizeof(game->activeUsers[c][s][u].name), "%s", p_name);
                                                        snprintf(game->activeUsers[c][s][u].mac, sizeof(game->activeUsers[c][s][u].mac), "%s", p_mac);
                                                        found = true;
                                                        break;
                                                    }
                                                    if(game->activeUsers[c][s][u].lastSeen < oldestTime) {
                                                        oldestTime = game->activeUsers[c][s][u].lastSeen;
                                                        oldestIdx = u;
                                                    }
                                                }
                                                if(!found) {
                                                    snprintf(game->activeUsers[c][s][oldestIdx].clientID, sizeof(game->activeUsers[c][s][oldestIdx].clientID), "%s", p_cid);
                                                    snprintf(game->activeUsers[c][s][oldestIdx].name, sizeof(game->activeUsers[c][s][oldestIdx].name), "%s", p_name);
                                                    snprintf(game->activeUsers[c][s][oldestIdx].mac, sizeof(game->activeUsers[c][s][oldestIdx].mac), "%s", p_mac);
                                                    game->activeUsers[c][s][oldestIdx].lastSeen = ts;
                                                }
                                            }
                                        }
                                        game->needsRedrawTop = true;
                                        game->needsRedrawBottom = true;
                                        game->lastNetworkActivity = osGetTime();
                                    }
                                }
                            }
                        } else {
                            decode_drawing(payload);
                        }
                    } else {
                        decode_drawing(payload);
                    }
                    free(payload);
                }
            }
        }
        i += total_packet_len;
    }

    if (i > 0) {
        memmove(recv_buf, recv_buf + i, recv_len - i);
        recv_len -= i;
    }
}

void handle_admin_command(const char* topic, const char* payload) {
    if (strcmp(topic, admin_reset_topic) == 0) {
        if (strcmp(payload, admin_token) == 0) {
            bannedCount = 0;
            lastBanChangeTime = (u64)getTrustedTime();
            saveBannedList();
            updateStatus("Ban list reset by admin");
        }
    }
    else if (strcmp(topic, admin_kick_topic) == 0) {
        char *token = strdup(payload);
        if (token) {
            char *mac = strchr(token, '|');
            if (mac) {
                *mac = '\0';
                mac++;
                if (strcmp(token, admin_token) == 0 && strlen(mac) > 0) {
                    if (strcmp(mac, game->macAddress) == 0) {
                        if (game->inChat) {
                            game->inChat = false;
                            game->appState = STATE_MAIN_MENU;
                            updateStatus("KICKED by admin");
                            char hb_topic[64];
                            snprintf(hb_topic, sizeof(hb_topic), "%s/Heartbeat/C%d/S%d",
                                     g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                            char leave_payload[64];
                            snprintf(leave_payload, sizeof(leave_payload), "!%s", game->clientID);
                            mqtt_publish(hb_topic, leave_payload, false);
                            game->needsRedrawTop = true;
                            game->needsRedrawBottom = true;
                        }
                    }
                    for (int c=0; c<CATEGORY_COUNT; c++) {
                        for (int s=0; s<MAX_SUB_ROOMS; s++) {
                            for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                                if(strcmp(game->activeUsers[c][s][u].mac, mac) == 0) {
                                    game->activeUsers[c][s][u].lastSeen = 0;
                                }
                            }
                        }
                    }
                }
            }
            free(token);
        }
    }
    else if (strcmp(topic, admin_unban_topic) == 0) {
        char *token = strdup(payload);
        if (token) {
            char *mac = strchr(token, '|');
            if (mac) {
                *mac = '\0';
                mac++;
                if (strcmp(token, admin_token) == 0 && strlen(mac) > 0) {
                    bool changed = false;
                    for(int i = 0; i < bannedCount; i++) {
                        if(strcmp(bannedUsers[i].mac, mac) == 0) {
                            for(int j = i; j < bannedCount - 1; j++) {
                                bannedUsers[j] = bannedUsers[j+1];
                            }
                            bannedCount--;
                            changed = true;
                            break;
                        }
                    }
                    if(changed) {
                        lastBanChangeTime = (u64)getTrustedTime();
                        saveBannedList();
                        if (strcmp(mac, game->macAddress) == 0) {
                            updateStatus("You were UNBANNED!");
                        }
                    }
                }
            }
            free(token);
        }
    }
    else if (strcmp(topic, admin_announce_topic) == 0) {
        char *token = strdup(payload);
        if (token) {
            char *msg = strchr(token, '|');
            if (msg) {
                *msg = '\0';
                msg++;
                if (strcmp(token, admin_token) == 0) {
                    updateStatus(msg);
                }
            }
            free(token);
        }
    }
    else if (strcmp(topic, admin_list_req_topic) == 0) {
        if (strcmp(payload, admin_token) == 0) {
            char* list_payload = (char*)malloc(4096);
            if (list_payload) {
                int offset = snprintf(list_payload, 4096, "%s|%llu|", admin_token, lastBanChangeTime);
                for(int i=0; i<bannedCount; i++) {
                    int written = snprintf(list_payload + offset, 4096 - offset, "%s,%s,%lld|", 
                        bannedUsers[i].mac, bannedUsers[i].name, (long long)bannedUsers[i].banEnd);
                    if (written > 0) offset += written;
                    if (offset >= 4090) break;
                }
                mqtt_publish(admin_list_res_topic, list_payload, false);
                free(list_payload);
            }
        }
    }
}