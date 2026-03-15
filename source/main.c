#include <citro2d.h>
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "secrets.h"

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define MAX_INK_LIMIT 650
#define MAX_STROKES 500
#define DRAW_THRESHOLD 25
#define MAX_PAYLOAD_SIZE 50000
#define MAX_MESSAGES 30
#define MAX_TEXT_LENGTH 150
#define CHARS_PER_LINE 35
#define MAX_ACTIVE_USERS 50
#define LOBBY_MAX_USERS 18
#define SCROLL_SPEED 12

#define DRAWING_AREA_TOP 25
#define DRAWING_AREA_BOTTOM 212
#define DRAWING_AREA_HEIGHT 240
#define DRAWING_PREVIEW_HEIGHT 105
#define TOOLBAR_Y_START 212
#define TOOLBAR_HEIGHT 28
#define MESSAGE_BOTTOM_PADDING 40
#define SCROLL_BAR_WIDTH 10
#define SCROLL_BAR_X 0
#define CONTENT_OFFSET_X 14

#define CATEGORY_COUNT 7
#define MAX_SUB_ROOMS 7

int SUB_ROOM_COUNTS[CATEGORY_COUNT] = {7, 3, 3, 7, 7, 3, 3};

const char* ROOM_NAMES[CATEGORY_COUNT] = {
    "Main Plaza", "Chaos Corner", "Doodle Room", "FC Exchange",
    "Matchmaking", "Retro Vibes", "Tech Support"
};

const char* ROOM_DESCRIPTIONS[CATEGORY_COUNT] = {
    "General chat and hanging out with others.",
    "Pure Chaos and Memes.",
    "Share your best drawings and art here.",
    "Swap Friend Codes to play together.",
    "Find players for multiplayer games.",
    "Discuss classic games and nostalgia.",
    "Get help with homebrew and hardware."
};

const char* SUB_ROOM_NAMES[CATEGORY_COUNT][MAX_SUB_ROOMS] = {
    {"English", "Espanol", "Deutsch", "Francais", "Portuguese", "Japanese", "International"},
    {"Lobby A", "Lobby B", "Lobby C", "", "", "", ""},
    {"Lobby A", "Lobby B", "Lobby C", "", "", "", ""},
    {"English", "Espanol", "Deutsch", "Francais", "Portuguese", "Japanese", "International"},
    {"Animal Crossing", "Mario Kart 7", "Mario Maker", "Smash Bros", "Monster Hunter", "Yo-Kai", "Others"},
    {"Lobby A", "Lobby B", "Lobby C", "", "", "", ""},
    {"Lobby A", "Lobby B", "Lobby C", "", "", "", ""}
};

#define ERASER_SIZE_COUNT 3
#define PEN_SIZE_COUNT 3
#define NUM_USER_COLORS 6

const u32 USER_COLORS[NUM_USER_COLORS] = {
    0xFFFFAA64, 
    0xFF6464FF, 
    0xFF64FF64, 
    0xFF64FFFF, 
    0xFFFF96FF, 
    0xFF00A5FF  
};

typedef enum {
    STATE_MAIN_MENU,
    STATE_SUB_MENU,
    STATE_CHAT
} AppState;

typedef struct {
    u16 x, y;
    u8 type;
    u8 sizeIdx;
} Point;

typedef struct {
    char text[MAX_TEXT_LENGTH];
    char sender[16];
    u8 nameColorIdx; 
    u32 timestamp;
    bool isDrawing;
    Point* drawingData;
    int drawingCount;
    int strokeStarts[MAX_STROKES];
    int strokeCount;
    int wrappedLines;
} ChatMessage;

typedef struct {
    ChatMessage messages[MAX_MESSAGES];
    int messageCount;
    int messageScrollOffset;
    bool autoScroll;
    bool hasMessages;
} RoomChat;

typedef struct {
    char clientID[32];
    time_t lastSeen;
} ActiveUser;

typedef struct {
    RoomChat rooms[CATEGORY_COUNT][MAX_SUB_ROOMS];
    time_t roomActivity[CATEGORY_COUNT][MAX_SUB_ROOMS];
    ActiveUser activeUsers[CATEGORY_COUNT][MAX_SUB_ROOMS][MAX_ACTIVE_USERS];
    Point userDrawing[MAX_INK_LIMIT];
    int userStrokeStarts[MAX_STROKES];
    int userDrawingCount;
    int userStrokeCount;
    int undoDrawingCount[MAX_STROKES];
    int undoStrokeCount[MAX_STROKES];
    int undoPtr;
    int redoDrawingCount[MAX_STROKES];
    int redoStrokeCount[MAX_STROKES];
    int redoPtr;
    char userName[16];
    u8 userColorIdx;
    char macAddress[13];
    char clientID[32];
    bool isEraser;
    int eraserSizeIdx;
    float eraserSizes[ERASER_SIZE_COUNT];
    int penSizeIdx;
    float penSizes[PEN_SIZE_COUNT];
    AppState appState;
    bool inChat;
    bool autoScrollEnabled;
    bool isSyncing;
    int selectedCategoryIdx;
    int selectedSubIdx;
    char statusMsg[64];
    u32 statusMsgTimer;
    bool needsRedrawTop;
    bool needsRedrawBottom;
    bool isDrawing;
    touchPosition lastValidTouch;
    bool hasUnsavedDrawing;
    u32 lastTouchTime;
    u32 lastTopUiUpdate;
} GameState;

GameState* game = NULL;
C3D_RenderTarget *top, *bottom;
C2D_TextBuf g_dynBuf;
static u32 *SOC_buffer = NULL;
int mqtt_sock = -1;
u32 last_ping_time = 0;
bool sendInProgress = false;

char g_mqtt_broker[64];
char g_base_topic[32];
int g_mqtt_port = 0;

void decrypt_secrets() {
    for(int i = 0; i < SECRET_BROKER_LEN; i++) {
        g_mqtt_broker[i] = SECRET_BROKER_XOR[i] ^ 0xAA;
    }
    g_mqtt_broker[SECRET_BROKER_LEN] = '\0';

    for(int i = 0; i < SECRET_TOPIC_LEN; i++) {
        g_base_topic[i] = SECRET_TOPIC_XOR[i] ^ 0xAA;
    }
    g_base_topic[SECRET_TOPIC_LEN] = '\0';

    g_mqtt_port = SECRET_PORT_XOR ^ 0x1234;
}

void updateStatus(const char* t) {
    if (game) {
        snprintf(game->statusMsg, 63, "%s", t);
        game->statusMsgTimer = osGetTime() + 2000;
        game->needsRedrawTop = true;
    }
}

int getActiveUserCount(int category, int subRoom) {
    int count = 0;
    time_t now = time(NULL);
    for (int u = 0; u < MAX_ACTIVE_USERS; u++) {
        if (game->activeUsers[category][subRoom][u].lastSeen > 0 &&
            (now - game->activeUsers[category][subRoom][u].lastSeen) <= 60) {
            count++;
        }
    }
    return count;
}

void ensureDirectoriesExist() {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/NoteRoom", 0777);
}

void saveUserData() {
    ensureDirectoriesExist();
    FILE* f = fopen("sdmc:/3ds/NoteRoom/user.txt", "w");
    if (f) {
        fprintf(f, "%s\n%d\n", game->userName, game->userColorIdx);
        fclose(f);
    }
}

void loadOrAskUsername() {
    ensureDirectoriesExist();
    FILE* f = fopen("sdmc:/3ds/NoteRoom/user.txt", "r");
    if (f) {
        fscanf(f, "%12s\n%hhu\n", game->userName, &game->userColorIdx);
        fclose(f);
        if(game->userColorIdx >= NUM_USER_COLORS) game->userColorIdx = 0;
        if (strlen(game->userName) > 0) return;
    } else {
        f = fopen("sdmc:/3ds/NoteRoom/name.txt", "r");
        if (f) {
            fscanf(f, "%12s", game->userName);
            fclose(f);
            game->userColorIdx = 0;
            saveUserData();
            if (strlen(game->userName) > 0) return;
        }
    }
    SwkbdState s;
    swkbdInit(&s, SWKBD_TYPE_NORMAL, 1, 12);
    swkbdSetHintText(&s, "Enter Nickname (Max 12)");
    swkbdSetValidation(&s, SWKBD_NOTEMPTY_NOTBLANK, 0, 12);
    if (swkbdInputText(&s, game->userName, 13) != SWKBD_BUTTON_CONFIRM) {
        snprintf(game->userName, 13, "Guest");
    } else {
        game->userName[12] = '\0'; 
    }
    game->userColorIdx = 0;
    saveUserData();
}

void editUsername() {
    SwkbdState s;
    char newName[16] = {0};
    swkbdInit(&s, SWKBD_TYPE_NORMAL, 1, 12);
    swkbdSetHintText(&s, "New Nickname (Max 12)");
    swkbdSetInitialText(&s, game->userName);
    swkbdSetValidation(&s, SWKBD_NOTEMPTY_NOTBLANK, 0, 12);
    if (swkbdInputText(&s, newName, 13) == SWKBD_BUTTON_CONFIRM) {
        snprintf(game->userName, 13, "%s", newName);
        saveUserData();
        updateStatus("Name updated!");
    }
}

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

void mqtt_connect() {
    updateStatus("Connecting...");
    game->needsRedrawTop = true;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C3D_FrameEnd(0);
    
    struct hostent* host = gethostbyname(g_mqtt_broker);
    if (!host) { updateStatus("DNS Error!"); return; }
    mqtt_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (mqtt_sock < 0) { updateStatus("Socket Error!"); return; }
    
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(g_mqtt_port); 
    server.sin_addr.s_addr = *((unsigned long*)host->h_addr_list[0]);
    if (connect(mqtt_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        updateStatus("Connect Failed!");
        close(mqtt_sock);
        mqtt_sock = -1;
        return;
    }
    
    u8 packet[128];
    int p = 0;
    packet[p++] = 0x10;
    int rem_len = 10 + 2 + strlen(game->clientID);
    p += mqtt_encode_length(&packet[p], rem_len);
    packet[p++] = 0x00; packet[p++] = 0x04;
    packet[p++] = 'M'; packet[p++] = 'Q'; packet[p++] = 'T'; packet[p++] = 'T';
    packet[p++] = 0x04; packet[p++] = 0x02;
    packet[p++] = 0x00; packet[p++] = 0x3C;
    packet[p++] = 0x00; packet[p++] = strlen(game->clientID);
    memcpy(&packet[p], game->clientID, strlen(game->clientID));
    p += strlen(game->clientID);
    send(mqtt_sock, packet, p, 0);
    fcntl(mqtt_sock, F_SETFL, O_NONBLOCK);
    updateStatus("Connected!");
    last_ping_time = osGetTime();
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
    send(mqtt_sock, packet, p, 0);
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
    send(mqtt_sock, packet, p, 0);
}

void mqtt_publish(const char* topic, const char* payload, bool retain) {
    if (mqtt_sock < 0) return;
    u8* packet = (u8*)malloc(MAX_PAYLOAD_SIZE);
    if (!packet) {
        updateStatus("Memory Error!");
        return;
    }
    int p = 0;
    packet[p++] = retain ? 0x31 : 0x30;
    int payload_len = strlen(payload);
    int rem_len = 2 + strlen(topic) + payload_len;
    p += mqtt_encode_length(&packet[p], rem_len);
    packet[p++] = 0x00; packet[p++] = strlen(topic);
    memcpy(&packet[p], topic, strlen(topic));
    p += strlen(topic);
    memcpy(&packet[p], payload, payload_len);
    p += payload_len;
    
    int res = send(mqtt_sock, packet, p, 0);
    if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(mqtt_sock);
        mqtt_sock = -1;
    }
    free(packet);
}

void mqtt_ping() {
    if (mqtt_sock < 0) return;
    u32 now = osGetTime();
    if (now - last_ping_time > 15000) {
        u8 packet[2] = {0xC0, 0x00};
        
        int res = send(mqtt_sock, packet, 2, 0);
        if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            close(mqtt_sock);
            mqtt_sock = -1; 
        } else {
            last_ping_time = now;
        }
    }
}

void addMessage(const char* sender, u8 colorIdx, const char* text, bool isDrawing, Point* drawingData, int drawingCount, int* strokeStarts, int strokeCount) {
    RoomChat* room = &game->rooms[game->selectedCategoryIdx][game->selectedSubIdx];
    if (room->messageCount >= MAX_MESSAGES) {
        if (room->messages[0].drawingData) {
            free(room->messages[0].drawingData);
            room->messages[0].drawingData = NULL;
        }
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            room->messages[i] = room->messages[i + 1];
        }
        room->messageCount--;
    }
    ChatMessage* msg = &room->messages[room->messageCount];
    memset(msg, 0, sizeof(ChatMessage));
    strncpy(msg->sender, sender, 15);
    msg->sender[15] = '\0';
    msg->nameColorIdx = colorIdx; 
    strncpy(msg->text, text, MAX_TEXT_LENGTH - 1);
    msg->text[MAX_TEXT_LENGTH - 1] = '\0';
    msg->isDrawing = isDrawing;
    msg->timestamp = (u32)(osGetTime() / 1000);
    
    if (!isDrawing && strlen(text) > 0) {
        int lines = 1;
        int text_len = strlen(text);
        int i = 0;
        while(i < text_len) {
            int chunk = CHARS_PER_LINE;
            if(i + chunk >= text_len) break;
            int space_idx = -1;
            for(int j = i + chunk; j > i; j--) {
                if(text[j] == ' ') { space_idx = j; break; }
            }
            if(space_idx != -1) {
                chunk = space_idx - i;
                i += chunk + 1;
            } else {
                i += chunk;
            }
            lines++;
        }
        msg->wrappedLines = lines;
        if (msg->wrappedLines > 6) msg->wrappedLines = 6;
    } else {
        msg->wrappedLines = 1;
    }
    
    if (isDrawing && drawingData && drawingCount > 0 && drawingCount < MAX_INK_LIMIT) {
        msg->drawingData = (Point*)malloc(sizeof(Point) * drawingCount);
        if (msg->drawingData) {
            memcpy(msg->drawingData, drawingData, sizeof(Point) * drawingCount);
            msg->drawingCount = drawingCount;
            if (strokeStarts && strokeCount > 0) {
                memcpy(msg->strokeStarts, strokeStarts, sizeof(int) * strokeCount);
                msg->strokeCount = strokeCount;
            }
        }
    }
    room->messageCount++;
    room->hasMessages = true;
    if (game->autoScrollEnabled) {
        room->autoScroll = true;
    }
    game->needsRedrawTop = true;
}

void decode_drawing(const char* payload) {
    char* firstPipe = strchr(payload, '|');
    if (!firstPipe) return;
    char senderName[16];
    int colorIdx = 0;
    char* hash = strchr(payload, '#');
    if (hash && hash < firstPipe) {
        int nameLen = hash - payload;
        if (nameLen > 12) nameLen = 12;
        strncpy(senderName, payload, nameLen);
        senderName[nameLen] = '\0';
        colorIdx = atoi(hash + 1);
    } else {
        int nameLen = firstPipe - payload;
        if (nameLen > 12) nameLen = 12;
        strncpy(senderName, payload, nameLen);
        senderName[nameLen] = '\0';
    }
    
    if (colorIdx < 0 || colorIdx >= NUM_USER_COLORS) colorIdx = 0;
    if (strcmp(senderName, game->userName) == 0) return;
    
    const char* content = firstPipe + 1;
    if (strncmp(content, "TEXT:", 5) == 0) {
        addMessage(senderName, colorIdx, content + 5, false, NULL, 0, NULL, 0);
        return;
    }
    
    Point* drawingPoints = (Point*)malloc(sizeof(Point) * MAX_INK_LIMIT);
    int* strokeStarts = (int*)malloc(sizeof(int) * MAX_STROKES);
    if (!drawingPoints || !strokeStarts) {
        if (drawingPoints) free(drawingPoints);
        if (strokeStarts) free(strokeStarts);
        return;
    }
    
    int pointIndex = 0;
    int strokeIndex = 0;
    int hexLen = strlen(content);
    for (int i = 0; i + 8 < hexLen && pointIndex < MAX_INK_LIMIT && strokeIndex < MAX_STROKES; i += 9) {
        char chunk[10] = {0};
        strncpy(chunk, content + i, 9);
        if (strcmp(chunk, "FFFFFFFF0") == 0) {
            if (pointIndex < MAX_INK_LIMIT) {
                drawingPoints[pointIndex++] = (Point){0xFFFF, 0xFFFF, 0, 0};
            }
            continue;
        }
        if (strlen(chunk) >= 9) {
            u8 x_low = (u8)strtol((char[]){chunk[0], chunk[1], '\0'}, NULL, 16);
            u8 x_high = (u8)strtol((char[]){chunk[2], chunk[3], '\0'}, NULL, 16);
            u8 y_low = (u8)strtol((char[]){chunk[4], chunk[5], '\0'}, NULL, 16);
            u8 y_high = (u8)strtol((char[]){chunk[6], chunk[7], '\0'}, NULL, 16);
            
            u8 encoded_type = (u8)strtol((char[]){chunk[8], '\0'}, NULL, 16);
            u8 type = encoded_type & 0x1;
            u8 sizeIdx = (encoded_type >> 1) & 0x3;
            
            u16 x = (u16)(x_low | (x_high << 8));
            u16 y = (u16)(y_low | (y_high << 8));
            if (x != 0xFFFF || y != 0xFFFF) {
                if (pointIndex == 0 || (pointIndex > 0 && drawingPoints[pointIndex-1].x == 0xFFFF)) {
                    if (strokeIndex < MAX_STROKES) { strokeStarts[strokeIndex++] = pointIndex; }
                }
                if (pointIndex < MAX_INK_LIMIT) {
                    drawingPoints[pointIndex++] = (Point){
                        .x = x, .y = y, .type = type, .sizeIdx = sizeIdx
                    };
                }
            }
        }
    }
    if (pointIndex > 0) {
        addMessage(senderName, colorIdx, "", true, drawingPoints, pointIndex, strokeStarts, strokeIndex);
    }
    free(drawingPoints);
    free(strokeStarts);
}

static u8 recv_buf[MAX_PAYLOAD_SIZE];
static int recv_len = 0;

void mqtt_poll() {
    if (mqtt_sock < 0) {
        updateStatus("Reconnecting...");
        mqtt_connect();
        if (mqtt_sock >= 0) {
            char topic_sub[64];
            sprintf(topic_sub, "%s/Heartbeat/#", g_base_topic);
            mqtt_subscribe(topic_sub);
            if (game->inChat) {
                char topic[64];
                sprintf(topic, "%s/Picto/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                mqtt_subscribe(topic);
            }
        }
        return; 
    }

    int bytes = recv(mqtt_sock, recv_buf + recv_len, MAX_PAYLOAD_SIZE - recv_len, 0);
    
    if (bytes > 0) {
        recv_len += bytes;
        int i = 0;
        while (i < recv_len) {
            if (recv_buf[i] == 0x30 || recv_buf[i] == 0x31) {
                bool isRetained = (recv_buf[i] == 0x31);
                i++;
                int multiplier = 1, rem_len = 0;
                u8 encodedByte;
                do {
                    if(i >= recv_len) { recv_len = 0; return; }
                    encodedByte = recv_buf[i++];
                    rem_len += (encodedByte & 127) * multiplier;
                    multiplier *= 128;
                } while ((encodedByte & 128) != 0);
                
                if (i + rem_len <= recv_len) {
                    int topic_len = (recv_buf[i] << 8) | recv_buf[i+1];
                    char topic_str[128];
                    if (topic_len < 127) {
                        memcpy(topic_str, &recv_buf[i+2], topic_len);
                        topic_str[topic_len] = '\0';
                    } else { topic_str[0] = '\0'; }
                    
                    i += 2 + topic_len;
                    int payload_len = rem_len - 2 - topic_len;
                    
                    if(payload_len > 0 && payload_len < MAX_PAYLOAD_SIZE) {
                        char* payload = (char*)malloc(payload_len + 1);
                        if (payload) {
                            memcpy(payload, &recv_buf[i], payload_len);
                            payload[payload_len] = '\0';
                            
                            char expected_hb[64];
                            sprintf(expected_hb, "%s/Heartbeat/C", g_base_topic);
                            
                            char expected_req[64];
                            sprintf(expected_req, "%s/Heartbeat/REQ", g_base_topic);
                            
                            if (strcmp(topic_str, expected_req) == 0) {
                                if (payload[0] == '?') {
                                    last_ping_time = 0; 
                                }
                            }
                            else if (strncmp(topic_str, expected_hb, strlen(expected_hb)) == 0) {
                                if (!isRetained) {
                                    int c, s;
                                    char format_str[64];
                                    sprintf(format_str, "%s/Heartbeat/C%%d/S%%d", g_base_topic);
                                    if (sscanf(topic_str, format_str, &c, &s) == 2) {
                                        if (c >= 0 && c < CATEGORY_COUNT && s >= 0 && s < MAX_SUB_ROOMS) {
                                            time_t ts = time(NULL);
                                            game->roomActivity[c][s] = ts;
                                            
                                            if (payload[0] == '!') {
                                                for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                                                    if(strcmp(game->activeUsers[c][s][u].clientID, payload + 1) == 0) {
                                                        game->activeUsers[c][s][u].lastSeen = 0;
                                                        break;
                                                    }
                                                }
                                            } else {
                                                bool found = false;
                                                int oldestIdx = 0;
                                                time_t oldestTime = ts;
                                                for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                                                    if(strcmp(game->activeUsers[c][s][u].clientID, payload) == 0) {
                                                        game->activeUsers[c][s][u].lastSeen = ts;
                                                        found = true;
                                                        break;
                                                    }
                                                    if(game->activeUsers[c][s][u].lastSeen < oldestTime) {
                                                        oldestTime = game->activeUsers[c][s][u].lastSeen;
                                                        oldestIdx = u;
                                                    }
                                                }
                                                if(!found) {
                                                    snprintf(game->activeUsers[c][s][oldestIdx].clientID, 32, "%s", payload);
                                                    game->activeUsers[c][s][oldestIdx].lastSeen = ts;
                                                }
                                            }
                                            game->needsRedrawTop = true;
                                        }
                                    }
                                }
                            } else {
                                decode_drawing(payload);
                            }
                            free(payload);
                        }
                    }
                    i += payload_len;
                } else break;
            } else i++;
        }
        if (i > 0) {
            memmove(recv_buf, recv_buf + i, recv_len - i);
            recv_len -= i;
        }
    } 
    else if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(mqtt_sock); 
        mqtt_sock = -1;
        recv_len = 0; 
    }
}

void saveUndoState() {
    if (game->undoPtr < MAX_STROKES) {
        game->undoDrawingCount[game->undoPtr] = game->userDrawingCount;
        game->undoStrokeCount[game->undoPtr] = game->userStrokeCount;
        game->undoPtr++;
        game->redoPtr = 0;
    }
}

void handleDrawingTouch(touchPosition touch, u32 currentTime) {
    if (game->userDrawingCount >= MAX_INK_LIMIT - 2) {
        if (game->isDrawing) {
            game->userDrawing[game->userDrawingCount++] = (Point){0xFFFF, 0xFFFF, 0, 0};
            saveUndoState();
            game->isDrawing = false;
            game->needsRedrawBottom = true;
        }
        return;
    }
    if (!game->isDrawing) {
        game->isDrawing = true;
        if (game->userStrokeCount < MAX_STROKES &&
            (game->userDrawingCount == 0 || game->userDrawing[game->userDrawingCount-1].x == 0xFFFF)) {
            game->userStrokeStarts[game->userStrokeCount++] = game->userDrawingCount;
        }
        game->userDrawing[game->userDrawingCount++] = (Point){
            .x = touch.px, .y = touch.py, 
            .type = (u8)(game->isEraser ? 1 : 0), 
            .sizeIdx = (u8)(game->isEraser ? game->eraserSizeIdx : game->penSizeIdx)
        };
        game->hasUnsavedDrawing = true;
        game->lastTouchTime = currentTime;
    } else {
        if (game->userDrawingCount > 0 && game->userDrawing[game->userDrawingCount-1].x != 0xFFFF) {
            Point last = game->userDrawing[game->userDrawingCount-1];
            int dx = touch.px - last.x;
            int dy = touch.py - last.y;
            if (dx*dx + dy*dy > DRAW_THRESHOLD && game->userDrawingCount < MAX_INK_LIMIT) {
                game->userDrawing[game->userDrawingCount++] = (Point){
                    .x = touch.px, .y = touch.py, 
                    .type = (u8)(game->isEraser ? 1 : 0), 
                    .sizeIdx = (u8)(game->isEraser ? game->eraserSizeIdx : game->penSizeIdx)
                };
                game->hasUnsavedDrawing = true;
                game->lastTouchTime = currentTime;
            }
        }
    }
    game->needsRedrawBottom = true;
}

void finishDrawingStroke() {
    if (game->isDrawing && game->userDrawingCount > 0) {
        Point lastStored = game->userDrawing[game->userDrawingCount-1];
        if (lastStored.x != 0xFFFF && (lastStored.x != game->lastValidTouch.px || lastStored.y != game->lastValidTouch.py)) {
            if (game->userDrawingCount < MAX_INK_LIMIT) {
                game->userDrawing[game->userDrawingCount++] = (Point){
                    .x = game->lastValidTouch.px, .y = game->lastValidTouch.py,
                    .type = (u8)(game->isEraser ? 1 : 0), 
                    .sizeIdx = (u8)(game->isEraser ? game->eraserSizeIdx : game->penSizeIdx)
                };
            }
        }
        if (game->userDrawingCount < MAX_INK_LIMIT) {
            game->userDrawing[game->userDrawingCount++] = (Point){0xFFFF, 0xFFFF, 0, 0};
            saveUndoState();
        }
        game->isDrawing = false;
        game->needsRedrawBottom = true;
    }
}

void sendDrawing() {
    if (sendInProgress) return;
    if (game->userDrawingCount <= 0) return;
    sendInProgress = true;
    updateStatus("Sending...");
    
    int payloadSize = strlen(game->userName) + 25 + (game->userDrawingCount * 10) + 1;
    if (payloadSize > MAX_PAYLOAD_SIZE) {
        updateStatus("Too large!");
        game->userDrawingCount = 0; game->userStrokeCount = 0; game->needsRedrawBottom = true;
        sendInProgress = false; return;
    }
    
    char* payload = (char*)malloc(payloadSize);
    if (!payload) { updateStatus("Memory Error!"); sendInProgress = false; return; }
    
    sprintf(payload, "%s#%d#%s|", game->userName, game->userColorIdx, game->macAddress);
    char* dest = payload + strlen(payload);
    
    for(int i = 0; i < game->userDrawingCount; i++) {
        Point p = game->userDrawing[i];
        if(p.x == 0xFFFF) { dest += sprintf(dest, "FFFFFFFF0"); }
        else { 
            u8 encoded_type = (p.sizeIdx << 1) | (p.type & 0x1);
            dest += sprintf(dest, "%02X%02X%02X%02X%01X", p.x & 0xFF, (p.x >> 8) & 0xFF, p.y & 0xFF, (p.y >> 8) & 0xFF, encoded_type); 
        }
    }
    
    char topic[64];
    sprintf(topic, "%s/Review/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
    mqtt_publish(topic, payload, false);
    free(payload);
    
    addMessage(game->userName, game->userColorIdx, "", true, game->userDrawing, game->userDrawingCount, game->userStrokeStarts, game->userStrokeCount);
    game->userDrawingCount = 0; game->userStrokeCount = 0;
    game->undoPtr = 1; game->undoDrawingCount[0] = 0; game->undoStrokeCount[0] = 0; game->redoPtr = 0;
    game->hasUnsavedDrawing = false; game->needsRedrawBottom = true;
    updateStatus("Sent!");
    svcSleepThread(500000000);
    sendInProgress = false;
}

void sendTextMessage(const char* text) {
    if (sendInProgress) return;
    if (!text || strlen(text) == 0) return;
    sendInProgress = true;
    
    int payloadSize = strlen(game->userName) + 25 + strlen(text) + 1;
    char* payload = (char*)malloc(payloadSize);
    if (payload) {
        sprintf(payload, "%s#%d#%s|TEXT:%s", game->userName, game->userColorIdx, game->macAddress, text);
        char topic[64];
        sprintf(topic, "%s/Review/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
        mqtt_publish(topic, payload, false);
        free(payload);
        
        addMessage(game->userName, game->userColorIdx, text, false, NULL, 0, NULL, 0);
        updateStatus("Sent!");
    }
    svcSleepThread(500000000);
    sendInProgress = false;
}

void openKeyboard() {
    SwkbdState s;
    char text[MAX_TEXT_LENGTH] = {0};
    swkbdInit(&s, SWKBD_TYPE_NORMAL, 2, MAX_TEXT_LENGTH - 1);
    swkbdSetHintText(&s, "Type message...");
    swkbdSetValidation(&s, SWKBD_NOTEMPTY_NOTBLANK, 0, MAX_TEXT_LENGTH - 1);
    if (swkbdInputText(&s, text, MAX_TEXT_LENGTH) == SWKBD_BUTTON_CONFIRM) {
        if (strlen(text) > 0) sendTextMessage(text);
    }
    game->needsRedrawBottom = true;
}

void renderDrawingPreview(ChatMessage* msg, float startX, float startY, float width, float height) {
    if (!msg || !msg->drawingData || msg->drawingCount < 2) return;
    
    float canvasW = 320.0f;
    float canvasH = (float)(DRAWING_AREA_BOTTOM - DRAWING_AREA_TOP);
    
    float scaleX = width / canvasW;
    float scaleY = height / canvasH;
    
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    
    float offsetX = startX + (width - (canvasW * scale)) / 2.0f;
    float offsetY = startY + (height - (canvasH * scale)) / 2.0f;
    
    for (int j = 1; j < msg->drawingCount; j++) {
        Point p1 = msg->drawingData[j-1];
        Point p2 = msg->drawingData[j];
        if (p1.x != 0xFFFF && p2.x != 0xFFFF) {
            float displaySize = (p2.type == 1) ? game->eraserSizes[p2.sizeIdx % ERASER_SIZE_COUNT] : game->penSizes[p2.sizeIdx % PEN_SIZE_COUNT];
            displaySize *= scale;
            if (displaySize < 1.0f) displaySize = 1.0f; 
            
            u32 col = (p2.type == 1) ? C2D_Color32(50, 55, 70, 255) : C2D_Color32(200, 200, 200, 255);
            
            float normY1 = (float)p1.y - DRAWING_AREA_TOP;
            float normY2 = (float)p2.y - DRAWING_AREA_TOP;
            
            float y1 = offsetY + normY1 * scale;
            float y2 = offsetY + normY2 * scale;
            float x1 = offsetX + (float)p1.x * scale;
            float x2 = offsetX + (float)p2.x * scale;
            
            C2D_DrawLine(x1, y1, col, x2, y2, col, displaySize, 0.5f);
        }
    }
}

void drawTextBubble(float x, float y, float width, float height, const char* sender, u8 colorIdx, const char* text, int wrappedLines) {
    C2D_DrawRectSolid(x, y, 0.5f, width, height, C2D_Color32(50, 55, 70, 255));
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x + width, y, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y + height, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x + width, y, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    
    char senderStr[32]; snprintf(senderStr, sizeof(senderStr), "%s", sender);
    C2D_Text t_sender; C2D_TextParse(&t_sender, g_dynBuf, senderStr);
    u32 nameColor = (colorIdx < NUM_USER_COLORS) ? USER_COLORS[colorIdx] : USER_COLORS[0];
    C2D_DrawText(&t_sender, C2D_WithColor | C2D_AlignLeft, x + 8, y + 4, 0.5f, 0.5f, 0.5f, nameColor);
    
    int textOffset = 0;
    float lineY = y + 22; 
    float textHeight = 18.0f;
    int text_len = strlen(text);
    
    for (int line = 0; line < wrappedLines && textOffset < text_len; line++) {
        int charsThisLine = CHARS_PER_LINE;
        if (textOffset + charsThisLine >= text_len) {
            charsThisLine = text_len - textOffset;
        } else {
            int space_idx = -1;
            for(int j = textOffset + charsThisLine; j > textOffset; j--) {
                if(text[j] == ' ') { space_idx = j; break; }
            }
            if(space_idx != -1) {
                charsThisLine = space_idx - textOffset;
            }
        }
        
        char lineText[CHARS_PER_LINE + 2] = {0};
        strncpy(lineText, text + textOffset, charsThisLine);
        lineText[charsThisLine] = '\0';
        
        C2D_Text t_content; C2D_TextParse(&t_content, g_dynBuf, lineText);
        C2D_DrawText(&t_content, C2D_WithColor, x + 8, lineY, 0.5f, 0.55f, 0.55f, C2D_Color32(255, 255, 255, 255));
        
        textOffset += charsThisLine;
        if (textOffset < text_len && text[textOffset] == ' ') {
            textOffset++;
        }
        lineY += textHeight;
    }
}

void drawDrawingBubble(float x, float y, float width, float height, const char* sender, u8 colorIdx, ChatMessage* msg) {
    C2D_DrawRectSolid(x, y, 0.5f, width, height, C2D_Color32(50, 55, 70, 255));
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x + width, y, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y + height, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x + width, y, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    
    char senderStr[32]; snprintf(senderStr, sizeof(senderStr), "%s", sender);
    C2D_Text t_sender; C2D_TextParse(&t_sender, g_dynBuf, senderStr);
    u32 nameColor = (colorIdx < NUM_USER_COLORS) ? USER_COLORS[colorIdx] : USER_COLORS[0];
    C2D_DrawText(&t_sender, C2D_WithColor | C2D_AlignLeft, x + 8, y + 4, 0.5f, 0.5f, 0.5f, nameColor);
    
    renderDrawingPreview(msg, x + 8, y + 22, width - 16, height - 26);
}

void renderScrollBar(float totalHeight, float visibleHeight, float scrollOffset) {
    if (totalHeight <= visibleHeight) return;
    float availableHeight = DRAWING_AREA_HEIGHT - 25 - 10;
    float barHeight = (visibleHeight / totalHeight) * availableHeight;
    if (barHeight < 20) barHeight = 20;
    if (barHeight > availableHeight) barHeight = availableHeight;
    float maxScroll = totalHeight - visibleHeight;
    float scrollPercent = (maxScroll > 0) ? (scrollOffset / maxScroll) : 0;
    if (scrollPercent > 1.0f) scrollPercent = 1.0f;
    float barY = 30 + scrollPercent * (availableHeight - barHeight);
    C2D_DrawRectSolid(SCROLL_BAR_X, 25, 0.5f, SCROLL_BAR_WIDTH, DRAWING_AREA_HEIGHT - 25, C2D_Color32(60, 60, 60, 255));
    C2D_DrawRectSolid(SCROLL_BAR_X + 2, barY, 0.5f, SCROLL_BAR_WIDTH - 4, barHeight, C2D_Color32(100, 100, 100, 255));
}

void drawActivityDot(float x, float y, int userCount) {
    u32 color = (userCount > 0) ? C2D_Color32(0, 255, 100, 255) : C2D_Color32(255, 255, 255, 255);
    C2D_DrawCircleSolid(x, y, 0.5f, 5.0f, color);
}

void renderTop() {
    if (!game->needsRedrawTop) return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    u8 battery_raw = 0;
    MCUHWC_GetBatteryLevel(&battery_raw); 
    if (battery_raw > 100) battery_raw = 100; // 
    
    u8 wifi = osGetWifiStrength();
    
    if (game->appState == STATE_MAIN_MENU || game->appState == STATE_SUB_MENU) {
        C2D_TargetClear(top, C2D_Color32(30, 30, 35, 255));
        C2D_SceneBegin(top);
        C2D_TextBufClear(g_dynBuf);
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 25, C2D_Color32(40, 45, 55, 255));
        C2D_DrawLine(0, 25, C2D_Color32(100, 180, 255, 255), 400, 25, C2D_Color32(100, 180, 255, 255), 2.0f, 0.5f);
        
        if (game->appState == STATE_MAIN_MENU) {
            C2D_Text t_title_name; C2D_TextParse(&t_title_name, g_dynBuf, game->userName);
            u32 nameColor = (game->userColorIdx < NUM_USER_COLORS) ? USER_COLORS[game->userColorIdx] : USER_COLORS[0];
            C2D_DrawText(&t_title_name, C2D_WithColor | C2D_AlignLeft, 5, 4, 0.5f, 0.6f, 0.6f, nameColor);
        } else {
            char titleStr[64];
            snprintf(titleStr, sizeof(titleStr), "%s", ROOM_NAMES[game->selectedCategoryIdx]);
            C2D_Text t_title; C2D_TextParse(&t_title, g_dynBuf, titleStr);
            C2D_DrawText(&t_title, C2D_WithColor | C2D_AlignLeft, 5, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        }
        
        if (game->isSyncing) {
            C2D_Text t_sync; C2D_TextParse(&t_sync, g_dynBuf, "Syncing...");
            C2D_DrawText(&t_sync, C2D_WithColor | C2D_AlignCenter, 200, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(180, 180, 180, 255));
        } else if (game->statusMsgTimer > osGetTime() && strstr(game->statusMsg, "FULL") != NULL) {
            C2D_Text t_stat; C2D_TextParse(&t_stat, g_dynBuf, game->statusMsg);
            C2D_DrawText(&t_stat, C2D_WithColor | C2D_AlignCenter, 200, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 50, 50, 255));
        } else {
            u32 connCol = (mqtt_sock >= 0) ? C2D_Color32(0, 255, 100, 255) : C2D_Color32(255, 255, 255, 255);
            const char* connText = (mqtt_sock >= 0) ? "Online" : "Offline";
            
            C2D_Text t_conn; C2D_TextParse(&t_conn, g_dynBuf, connText);
            float w_conn, h_conn;
            C2D_TextGetDimensions(&t_conn, 0.5f, 0.6f, &w_conn, &h_conn);
            
            float total_w = 15.0f + w_conn; 
            float start_x = 200.0f - (total_w / 2.0f); 
            
            C2D_DrawCircleSolid(start_x + 5.0f, 12.0f, 0.5f, 5.0f, connCol);
            C2D_DrawText(&t_conn, C2D_WithColor | C2D_AlignLeft, start_x + 15.0f, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        }
        
        
        float wifi_x = 310; 
        float wifi_y = 17;
        u32 wCol = C2D_Color32(200, 200, 200, 255);
        C2D_DrawCircleSolid(wifi_x, wifi_y, 0.5f, 2.0f, (wifi >= 1) ? wCol : C2D_Color32(80, 80, 80, 255));
        C2D_DrawRectSolid(wifi_x + 4, wifi_y - 4, 0.5f, 3, 6, (wifi >= 2) ? wCol : C2D_Color32(80, 80, 80, 255));
        C2D_DrawRectSolid(wifi_x + 9, wifi_y - 8, 0.5f, 3, 10, (wifi >= 3) ? wCol : C2D_Color32(80, 80, 80, 255));
        
        char sysInfo[32]; snprintf(sysInfo, sizeof(sysInfo), "Bat:%d%%", battery_raw);
        C2D_Text t_sys; C2D_TextParse(&t_sys, g_dynBuf, sysInfo);
        
        C2D_DrawText(&t_sys, C2D_WithColor | C2D_AlignRight, 395, 5, 0.5f, 0.6f, 0.6f, C2D_Color32(180, 180, 200, 255));
        
        int roomWidth = 340, roomHeight = 22, startX = (400 - roomWidth) / 2, startY = 38, spacing = 3;
        int itemCount = (game->appState == STATE_MAIN_MENU) ? CATEGORY_COUNT : SUB_ROOM_COUNTS[game->selectedCategoryIdx];
        int selectedItem = (game->appState == STATE_MAIN_MENU) ? game->selectedCategoryIdx : game->selectedSubIdx;
        
        for(int i = 0; i < itemCount; i++) {
            int ry = startY + i * (roomHeight + spacing);
            u32 bgColor = (i == selectedItem) ? C2D_Color32(70, 100, 140, 255) : C2D_Color32(50, 55, 65, 255);
            C2D_DrawRectSolid(startX, ry, 0.5f, roomWidth, roomHeight, bgColor);
            
            const char* itemName = (game->appState == STATE_MAIN_MENU) ? ROOM_NAMES[i] : SUB_ROOM_NAMES[game->selectedCategoryIdx][i];
            C2D_Text t_lobby; C2D_TextParse(&t_lobby, g_dynBuf, itemName);
            C2D_DrawText(&t_lobby, C2D_WithColor, startX + 10, ry + 4, 0.55f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
            
            int totalUsers = 0;
            char uCountStr[16]; 
            if (game->appState == STATE_MAIN_MENU) {
                for (int s = 0; s < SUB_ROOM_COUNTS[i]; s++) { 
                    totalUsers += getActiveUserCount(i, s); 
                }
                snprintf(uCountStr, sizeof(uCountStr), "%d", totalUsers);
            } else {
                totalUsers = getActiveUserCount(game->selectedCategoryIdx, i);
                snprintf(uCountStr, sizeof(uCountStr), "%d/%d", totalUsers, LOBBY_MAX_USERS);
            }
            
            C2D_Text t_users; C2D_TextParse(&t_users, g_dynBuf, uCountStr);
            C2D_DrawText(&t_users, C2D_WithColor | C2D_AlignRight, startX + roomWidth - 30, ry + 5, 0.55f, 0.5f, 0.5f, C2D_Color32(180, 200, 255, 255));
            drawActivityDot(startX + roomWidth - 15, ry + 11, totalUsers);
        }
        
        const char* inst;
        u32 instCol = C2D_Color32(150, 150, 170, 255);
        if (game->appState == STATE_MAIN_MENU) {
            if (game->isSyncing) {
                inst = "[ ] Syncing...   [X] Name   [Y] Color   [START] Exit";
                instCol = C2D_Color32(100, 100, 100, 255);
            } else {
                inst = "[A] Select   [X] Name   [Y] Color   [START] Exit";
            }
        } else {
            inst = "[A] Join   [B] Back   [START] Exit";
        }
        C2D_Text t_ins; C2D_TextParse(&t_ins, g_dynBuf, inst);
        C2D_DrawText(&t_ins, C2D_WithColor | C2D_AlignCenter, 200, 218, 0.5f, 0.55f, 0.55f, instCol);
        
    } else if (game->appState == STATE_CHAT) {
        C2D_TargetClear(top, C2D_Color32(25, 25, 30, 255));
        C2D_SceneBegin(top);
        C2D_TextBufClear(g_dynBuf);
        RoomChat* room = &game->rooms[game->selectedCategoryIdx][game->selectedSubIdx];
        
        if (room->messageCount > 0) {
            float textHeight = 18.0f, drawingHeight = DRAWING_PREVIEW_HEIGHT + 25, totalMsgHeight = 0; 
            for (int i = 0; i < room->messageCount; i++) {
                totalMsgHeight += (room->messages[i].isDrawing ? drawingHeight : (textHeight * room->messages[i].wrappedLines + 25)) + 5;
            }
            int maxS = (int)(totalMsgHeight - (DRAWING_AREA_HEIGHT - 25) + MESSAGE_BOTTOM_PADDING);
            if (maxS < 0) maxS = 0;
            if (room->autoScroll) { room->messageScrollOffset = maxS; room->autoScroll = false; }
            else {
                if (room->messageScrollOffset > maxS) room->messageScrollOffset = maxS;
                if (room->messageScrollOffset < 0) room->messageScrollOffset = 0;
            }
            float msgY = 25 + 5 - room->messageScrollOffset;
            for (int i = 0; i < room->messageCount; i++) {
                ChatMessage* msg = &room->messages[i];
                if (!msg) continue;
                float currentItemHeight = msg->isDrawing ? drawingHeight : (textHeight * msg->wrappedLines + 25);
                if (msgY + currentItemHeight > 0 && msgY < DRAWING_AREA_HEIGHT) {
                    u8 colorIdx = (msg->nameColorIdx < NUM_USER_COLORS) ? msg->nameColorIdx : 0;
                    if (msg->isDrawing) { 
                        drawDrawingBubble(50, msgY, 300, drawingHeight, msg->sender, colorIdx, msg); 
                    } else { 
                        drawTextBubble(50, msgY, 300, textHeight * msg->wrappedLines + 25, msg->sender, colorIdx, msg->text, msg->wrappedLines); 
                    }
                }
                msgY += currentItemHeight + 5;
            }
            renderScrollBar(totalMsgHeight, DRAWING_AREA_HEIGHT - 25, room->messageScrollOffset);
        } else {
            C2D_Text t_wait; C2D_TextParse(&t_wait, g_dynBuf, "Waiting for messages...");
            C2D_DrawText(&t_wait, C2D_WithColor | C2D_AlignCenter, 200, 110, 0.5f, 0.6f, 0.6f, C2D_Color32(100, 100, 100, 255));
        }
        
        C2D_Flush();
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 25, C2D_Color32(40, 45, 55, 255));
        C2D_DrawLine(0, 25, C2D_Color32(100, 180, 255, 255), 400, 25, C2D_Color32(100, 180, 255, 255), 2.0f, 0.5f);
        
        int activeCount = getActiveUserCount(game->selectedCategoryIdx, game->selectedSubIdx);
        char header_title[64]; snprintf(header_title, sizeof(header_title), "%s - %s   ", ROOM_NAMES[game->selectedCategoryIdx], SUB_ROOM_NAMES[game->selectedCategoryIdx][game->selectedSubIdx]);
        C2D_Text t_lobby_ind; C2D_TextParse(&t_lobby_ind, g_dynBuf, header_title);
        C2D_DrawText(&t_lobby_ind, C2D_WithColor | C2D_AlignLeft, 5, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        
        float tw, th;
        C2D_TextGetDimensions(&t_lobby_ind, 0.6f, 0.6f, &tw, &th);
        drawActivityDot(5 + tw + 5, 12, activeCount);
        
        char header_num[16]; snprintf(header_num, sizeof(header_num), "%d/%d", activeCount, LOBBY_MAX_USERS);
        C2D_Text t_num; C2D_TextParse(&t_num, g_dynBuf, header_num);
        C2D_DrawText(&t_num, C2D_WithColor | C2D_AlignLeft, 5 + tw + 15, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        
        
        float wifi_x = 310;
        float wifi_y = 17;
        u32 wCol = C2D_Color32(200, 200, 200, 255);
        C2D_DrawCircleSolid(wifi_x, wifi_y, 0.5f, 2.0f, (wifi >= 1) ? wCol : C2D_Color32(80, 80, 80, 255));
        C2D_DrawRectSolid(wifi_x + 4, wifi_y - 4, 0.5f, 3, 6, (wifi >= 2) ? wCol : C2D_Color32(80, 80, 80, 255));
        C2D_DrawRectSolid(wifi_x + 9, wifi_y - 8, 0.5f, 3, 10, (wifi >= 3) ? wCol : C2D_Color32(80, 80, 80, 255));
        
        char sysInfo[32]; snprintf(sysInfo, sizeof(sysInfo), "Bat:%d%%", battery_raw);
        C2D_Text t_sys; C2D_TextParse(&t_sys, g_dynBuf, sysInfo);
        C2D_DrawText(&t_sys, C2D_WithColor | C2D_AlignRight, 395, 5, 0.5f, 0.6f, 0.6f, C2D_Color32(180, 180, 200, 255));
    }
    C3D_FrameEnd(0);
    game->needsRedrawTop = false;
}

void drawDrawingToolbar() {
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 25, C2D_Color32(40, 40, 45, 255));
    
    C2D_Text t_undo; C2D_TextParse(&t_undo, g_dynBuf, "L=Undo");
    C2D_DrawText(&t_undo, C2D_WithColor, 5, 6, 0.5f, 0.6f, 0.6f, C2D_Color32(200, 200, 200, 255));
    
    C2D_Text t_redo; C2D_TextParse(&t_redo, g_dynBuf, "R=Redo");
    C2D_DrawText(&t_redo, C2D_WithColor | C2D_AlignRight, 315, 6, 0.5f, 0.6f, 0.6f, C2D_Color32(200, 200, 200, 255));
    
    char ascStr[32]; snprintf(ascStr, sizeof(ascStr), "Y=Scroll");
    C2D_Text t_asc; C2D_TextParse(&t_asc, g_dynBuf, ascStr);
    float w_asc, h_asc;
    C2D_TextGetDimensions(&t_asc, 0.5f, 0.6f, &w_asc, &h_asc);
    
    float currentSize = game->isEraser ? game->eraserSizes[game->eraserSizeIdx] : game->penSizes[game->penSizeIdx];
    char xStr[32]; snprintf(xStr, sizeof(xStr), "X=%.0fpx", currentSize);
    C2D_Text t_x; C2D_TextParse(&t_x, g_dynBuf, xStr);
    float w_x, h_x;
    C2D_TextGetDimensions(&t_x, 0.5f, 0.6f, &w_x, &h_x);
    
    float gap = 15.0f;
    float total_w = w_asc + gap + w_x;
    float start_x = 160.0f - (total_w / 2.0f);
    
    u32 ascCol = game->autoScrollEnabled ? C2D_Color32(50, 255, 50, 255) : C2D_Color32(255, 50, 50, 255);
    C2D_DrawText(&t_asc, C2D_WithColor | C2D_AlignLeft, start_x, 6, 0.5f, 0.6f, 0.6f, ascCol);
    C2D_DrawText(&t_x, C2D_WithColor | C2D_AlignLeft, start_x + w_asc + gap, 6, 0.5f, 0.6f, 0.6f, C2D_Color32(100, 180, 255, 255));
}

void renderBottom() {
    if (!game->needsRedrawBottom) return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(bottom, C2D_Color32(35, 35, 40, 255));
    C2D_SceneBegin(bottom);
    C2D_TextBufClear(g_dynBuf);
    
    if (game->appState == STATE_CHAT) {
        drawDrawingToolbar();
        
        if (game->userDrawingCount > 0) {
            float inkPercent = (float)game->userDrawingCount / MAX_INK_LIMIT;
            if (inkPercent > 1.0f) inkPercent = 1.0f;
            u32 inkColor = C2D_Color32(100, 255, 100, 255);
            if (inkPercent > 0.75f) inkColor = C2D_Color32(255, 200, 0, 255);
            if (inkPercent > 0.95f) inkColor = C2D_Color32(255, 50, 50, 255);
            C2D_DrawRectSolid(0, DRAWING_AREA_BOTTOM - 2, 0.5f, 320 * inkPercent, 2, inkColor);
        }

        if (game->userDrawingCount > 1) {
            for (int i = 1; i < game->userDrawingCount; i++) {
                Point p1 = game->userDrawing[i-1]; Point p2 = game->userDrawing[i];
                if (p1.x == 0xFFFF || p2.x == 0xFFFF) continue;
                float displaySize = p2.type == 1 ? game->eraserSizes[p2.sizeIdx % ERASER_SIZE_COUNT] : game->penSizes[p2.sizeIdx % PEN_SIZE_COUNT];
                u32 col = p2.type == 1 ? C2D_Color32(35, 35, 40, 255) : C2D_Color32(200, 200, 200, 255);
                C2D_DrawLine(p1.x, p1.y, col, p2.x, p2.y, col, displaySize, 0);
            }
        }
        
        if ((hidKeysHeld() & KEY_TOUCH) && game->userDrawingCount > 0) {
            touchPosition touch; hidTouchRead(&touch);
            if (touch.py >= DRAWING_AREA_TOP && touch.py < DRAWING_AREA_BOTTOM && game->userDrawingCount > 0) {
                Point last = game->userDrawing[game->userDrawingCount-1];
                if (last.x != 0xFFFF) {
                    float size = game->isEraser ? game->eraserSizes[game->eraserSizeIdx] : game->penSizes[game->penSizeIdx];
                    u32 col = game->isEraser ? C2D_Color32(35, 35, 40, 255) : C2D_Color32(200, 200, 200, 255);
                    C2D_DrawLine(last.x, last.y, col, touch.px, touch.py, col, size, 0);
                }
            }
        }
        
        const int btnY = TOOLBAR_Y_START, btnH = TOOLBAR_HEIGHT;
        C2D_DrawRectSolid(0, btnY, 0.5f, 64, btnH, C2D_Color32(180, 50, 50, 255));
        C2D_Text t_clear; C2D_TextParse(&t_clear, g_dynBuf, "CLR");
        C2D_DrawText(&t_clear, C2D_WithColor | C2D_AlignCenter, 32, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        u32 penCol = game->isEraser ? C2D_Color32(100, 100, 100, 255) : C2D_Color32(50, 120, 200, 255);
        C2D_DrawRectSolid(64, btnY, 0.5f, 64, btnH, penCol);
        C2D_Text t_pen; C2D_TextParse(&t_pen, g_dynBuf, "PEN");
        C2D_DrawText(&t_pen, C2D_WithColor | C2D_AlignCenter, 96, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        u32 eraserCol = game->isEraser ? C2D_Color32(50, 120, 200, 255) : C2D_Color32(100, 100, 100, 255);
        C2D_DrawRectSolid(128, btnY, 0.5f, 64, btnH, eraserCol);
        C2D_Text t_eraser; C2D_TextParse(&t_eraser, g_dynBuf, "ERS");
        C2D_DrawText(&t_eraser, C2D_WithColor | C2D_AlignCenter, 160, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        C2D_DrawRectSolid(192, btnY, 0.5f, 64, btnH, C2D_Color32(150, 50, 150, 255));
        C2D_Text t_text; C2D_TextParse(&t_text, g_dynBuf, "TXT");
        C2D_DrawText(&t_text, C2D_WithColor | C2D_AlignCenter, 224, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        C2D_DrawRectSolid(256, btnY, 0.5f, 64, btnH, C2D_Color32(50, 150, 50, 255));
        C2D_Text t_send; C2D_TextParse(&t_send, g_dynBuf, "SEND");
        C2D_DrawText(&t_send, C2D_WithColor | C2D_AlignCenter, 288, btnY+7, 0.5f, 0.55f, 0.55f, C2D_Color32(255, 255, 255, 255));
        
    } else {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, C2D_Color32(35, 35, 40, 255));
        C2D_Text t_title; C2D_TextParse(&t_title, g_dynBuf, "NoteRoom");
        C2D_DrawText(&t_title, C2D_WithColor | C2D_AlignCenter, 160, 12, 0.6f, 0.9f, 0.9f, C2D_Color32(255, 200, 80, 255));
        C2D_DrawLine(40, 45, C2D_Color32(100, 180, 255, 255), 280, 45, C2D_Color32(100, 180, 255, 255), 2.0f, 0.5f);
        
        char infoHeader[64]; snprintf(infoHeader, sizeof(infoHeader), "Channel: %s", ROOM_NAMES[game->selectedCategoryIdx]);
        C2D_Text t_ch_name; C2D_TextParse(&t_ch_name, g_dynBuf, infoHeader);
        C2D_DrawText(&t_ch_name, C2D_WithColor | C2D_AlignCenter, 160, 60, 0.5f, 0.65f, 0.65f, C2D_Color32(255, 255, 255, 255));
        C2D_Text t_desc; C2D_TextParse(&t_desc, g_dynBuf, ROOM_DESCRIPTIONS[game->selectedCategoryIdx]);
        C2D_DrawText(&t_desc, C2D_WithColor | C2D_AlignCenter, 160, 85, 0.5f, 0.55f, 0.55f, C2D_Color32(150, 220, 150, 255));
        C2D_DrawLine(40, 120, C2D_Color32(100, 100, 100, 255), 280, 120, C2D_Color32(100, 100, 100, 255), 1.0f, 0.5f);
        
        C2D_Text t_rules_header; C2D_TextParse(&t_rules_header, g_dynBuf, "--- SERVER RULES ---");
        C2D_DrawText(&t_rules_header, C2D_WithColor | C2D_AlignCenter, 160, 135, 0.5f, 0.5f, 0.5f, C2D_Color32(200, 100, 100, 255));
        C2D_Text t_r1; C2D_TextParse(&t_r1, g_dynBuf, "1. Be respectful to all users.");
        C2D_DrawText(&t_r1, C2D_WithColor | C2D_AlignCenter, 160, 155, 0.5f, 0.5f, 0.5f, C2D_Color32(150, 150, 150, 255));
        C2D_Text t_r2; C2D_TextParse(&t_r2, g_dynBuf, "2. No NSFW content or drawings.");
        C2D_DrawText(&t_r2, C2D_WithColor | C2D_AlignCenter, 160, 170, 0.5f, 0.5f, 0.5f, C2D_Color32(150, 150, 150, 255));
        C2D_Text t_r3; C2D_TextParse(&t_r3, g_dynBuf, "3. Do not spam the chat.");
        C2D_DrawText(&t_r3, C2D_WithColor | C2D_AlignCenter, 160, 185, 0.5f, 0.5f, 0.5f, C2D_Color32(150, 150, 150, 255));

        C2D_Text t_version; C2D_TextParse(&t_version, g_dynBuf, "v1.0  |  Beta Test ARLO");
        C2D_DrawText(&t_version, C2D_WithColor, 5, 220, 0.5f, 0.5f, 0.5f, C2D_Color32(80, 80, 90, 255));
    }
    C3D_FrameEnd(0);
    game->needsRedrawBottom = false;
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    
    mcuHwcInit();
    
    psInit();
    u64 fc_seed = 0;
    PS_GetLocalFriendCodeSeed(&fc_seed);
    psExit();

    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_dynBuf = C2D_TextBufNew(4096 * 2);
    SOC_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if(SOC_buffer) socInit(SOC_buffer, SOC_BUFFERSIZE);
    
    game = (GameState*)calloc(1, sizeof(GameState));
    
    game->autoScrollEnabled = true;
    game->isSyncing = true;
    snprintf(game->macAddress, sizeof(game->macAddress), "%012llX", fc_seed);
    
    decrypt_secrets();
    
    game->penSizes[0] = 2.0f; game->penSizes[1] = 4.0f; game->penSizes[2] = 8.0f;
    game->penSizeIdx = 0;
    game->eraserSizes[0] = 5.0f; game->eraserSizes[1] = 10.0f; game->eraserSizes[2] = 20.0f;
    game->eraserSizeIdx = 0;
    
    game->appState = STATE_MAIN_MENU;
    game->needsRedrawTop = true;
    game->needsRedrawBottom = true;
    loadOrAskUsername();
    snprintf(game->clientID, 32, "3ds_%d_%d", (int)osGetTime(), rand() % 1000);
    
    mqtt_connect();
    
    char topic_sub_hb[64];
    sprintf(topic_sub_hb, "%s/Heartbeat/#", g_base_topic);
    mqtt_subscribe(topic_sub_hb);
    
    time_t last_hb_time = 0;
    u32 boot_time = osGetTime();
    bool initial_req_sent = false;
    
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        u32 kUp = hidKeysUp();
        
        if (kDown & KEY_START) break;
        u32 currentTime = osGetTime();
        
        if (game->isSyncing && (currentTime - boot_time > 7000)) {
            game->isSyncing = false;
            game->needsRedrawTop = true;
        }
        
        if (!initial_req_sent && mqtt_sock >= 0) {
            char req_topic[64];
            sprintf(req_topic, "%s/Heartbeat/REQ", g_base_topic);
            mqtt_publish(req_topic, "?", false);
            initial_req_sent = true;
        }
        
        static u32 last_sec_update = 0;
        if (currentTime - last_sec_update > 1000) {
            game->needsRedrawTop = true;
            last_sec_update = currentTime;
        }
        
        if ((game->appState == STATE_MAIN_MENU || game->appState == STATE_SUB_MENU) &&
            (currentTime - game->lastTopUiUpdate) > 2000) {
            game->needsRedrawTop = true;
            game->lastTopUiUpdate = currentTime;
        }
        
        if (game->appState == STATE_MAIN_MENU) {
            if (kDown & KEY_UP) { game->selectedCategoryIdx = (game->selectedCategoryIdx - 1 + CATEGORY_COUNT) % CATEGORY_COUNT; game->needsRedrawTop = true; game->needsRedrawBottom = true;}
            if (kDown & KEY_DOWN) { game->selectedCategoryIdx = (game->selectedCategoryIdx + 1) % CATEGORY_COUNT; game->needsRedrawTop = true; game->needsRedrawBottom = true;}
            if (kDown & KEY_A) { 
                if (!game->isSyncing) {
                    game->selectedSubIdx = 0; 
                    game->appState = STATE_SUB_MENU; 
                    game->needsRedrawTop = true; 
                    game->needsRedrawBottom = true; 
                }
            }
            if (kDown & KEY_X) { editUsername(); }
            if (kDown & KEY_Y) {
                game->userColorIdx = (game->userColorIdx + 1) % NUM_USER_COLORS;
                saveUserData();
                game->needsRedrawTop = true;
            }
        }
        else if (game->appState == STATE_SUB_MENU) {
            if (kDown & KEY_UP) { game->selectedSubIdx = (game->selectedSubIdx - 1 + SUB_ROOM_COUNTS[game->selectedCategoryIdx]) % SUB_ROOM_COUNTS[game->selectedCategoryIdx]; game->needsRedrawTop = true; }
            if (kDown & KEY_DOWN) { game->selectedSubIdx = (game->selectedSubIdx + 1) % SUB_ROOM_COUNTS[game->selectedCategoryIdx]; game->needsRedrawTop = true; }
            if (kDown & KEY_B) { game->appState = STATE_MAIN_MENU; game->needsRedrawTop = true; game->needsRedrawBottom = true; }
            if (kDown & KEY_A) {
                if (getActiveUserCount(game->selectedCategoryIdx, game->selectedSubIdx) >= LOBBY_MAX_USERS) {
                    updateStatus("Lobby is FULL!");
                } else {
                    game->appState = STATE_CHAT;
                    game->inChat = true;
                    char topic[64];
                    sprintf(topic, "%s/Picto/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                    mqtt_subscribe(topic);
                    last_hb_time = 0; 
                    game->needsRedrawTop = true;
                    game->needsRedrawBottom = true;
                }
            }
        }
        else if (game->appState == STATE_CHAT) {
            if (kDown & KEY_B) {
                char hb_topic[64];
                sprintf(hb_topic, "%s/Heartbeat/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                char leave_payload[64];
                snprintf(leave_payload, sizeof(leave_payload), "!%s", game->clientID);
                mqtt_publish(hb_topic, leave_payload, false);
                
                char topic[64];
                sprintf(topic, "%s/Picto/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                mqtt_unsubscribe(topic);
                
                RoomChat* room = &game->rooms[game->selectedCategoryIdx][game->selectedSubIdx];
                for (int i = 0; i < room->messageCount; i++) {
                    if (room->messages[i].drawingData) {
                        free(room->messages[i].drawingData);
                        room->messages[i].drawingData = NULL;
                    }
                }
                room->messageCount = 0;
                room->hasMessages = false;
                room->messageScrollOffset = 0;
                room->autoScroll = true;
                
                game->userDrawingCount = 0;
                game->userStrokeCount = 0;
                game->undoPtr = 0;
                game->redoPtr = 0;
                game->hasUnsavedDrawing = false;
                
                game->inChat = false;
                game->appState = STATE_SUB_MENU;
                game->needsRedrawTop = true;
                game->needsRedrawBottom = true;
            }
            
            RoomChat* room = &game->rooms[game->selectedCategoryIdx][game->selectedSubIdx];
            
            if (kHeld & KEY_DUP) {
                room->messageScrollOffset -= SCROLL_SPEED;
                room->autoScroll = false;
                if (room->messageScrollOffset < 0) room->messageScrollOffset = 0;
                game->needsRedrawTop = true;
            }
            if (kHeld & KEY_DDOWN) {
                room->messageScrollOffset += SCROLL_SPEED;
                room->autoScroll = false;
                game->needsRedrawTop = true;
            }
            
            circlePosition pos;
            hidCircleRead(&pos);
            if (abs(pos.dy) > 20) {
                room->messageScrollOffset -= (pos.dy / 8);
                room->autoScroll = false;
                if (room->messageScrollOffset < 0) room->messageScrollOffset = 0;
                game->needsRedrawTop = true;
            }
            
            if (kHeld & KEY_TOUCH) {
                touchPosition touch;
                hidTouchRead(&touch);
                if (touch.py >= DRAWING_AREA_TOP && touch.py < DRAWING_AREA_BOTTOM) { 
                    game->lastValidTouch = touch; 
                    handleDrawingTouch(touch, currentTime); 
                }
            } 
            if (kUp & KEY_TOUCH) { 
                finishDrawingStroke();
            }
            
            if ((kDown & KEY_TOUCH)) {
                touchPosition touch;
                hidTouchRead(&touch);
                if (touch.py >= TOOLBAR_Y_START) {
                    if (touch.px < 64) { game->userDrawingCount = 0; game->userStrokeCount = 0; game->needsRedrawBottom = true; }
                    else if (touch.px < 128) { game->isEraser = false; game->needsRedrawBottom = true; }
                    else if (touch.px < 192) { game->isEraser = true; game->needsRedrawBottom = true; }
                    else if (touch.px < 256) { openKeyboard(); }
                    else { sendDrawing(); }
                }
            }
            
            if (kDown & KEY_L) {
                if (game->undoPtr > 0) {
                    game->redoDrawingCount[game->redoPtr] = game->userDrawingCount;
                    game->redoStrokeCount[game->redoPtr] = game->userStrokeCount;
                    game->redoPtr++; game->undoPtr--;
                    game->userDrawingCount = game->undoDrawingCount[game->undoPtr];
                    game->userStrokeCount = game->undoStrokeCount[game->undoPtr];
                    game->needsRedrawBottom = true;
                }
            }
            if (kDown & KEY_R) {
                if (game->redoPtr > 0) {
                    game->undoDrawingCount[game->undoPtr] = game->userDrawingCount;
                    game->undoStrokeCount[game->undoPtr] = game->userStrokeCount;
                    game->undoPtr++;
                    
                    game->redoPtr--;
                    game->userDrawingCount = game->redoDrawingCount[game->redoPtr];
                    game->userStrokeCount = game->redoStrokeCount[game->redoPtr];
                    
                    game->needsRedrawBottom = true;
                }
            }
            if (kDown & KEY_Y) {
                game->autoScrollEnabled = !game->autoScrollEnabled;
                game->needsRedrawBottom = true;
            }
            if (kDown & KEY_X) {
                if (game->isEraser) {
                    game->eraserSizeIdx = (game->eraserSizeIdx + 1) % ERASER_SIZE_COUNT;
                } else {
                    game->penSizeIdx = (game->penSizeIdx + 1) % PEN_SIZE_COUNT;
                }
                game->needsRedrawBottom = true;
            }
        }
        
        mqtt_poll();
        mqtt_ping(); 
        
        time_t now = time(NULL);
        
        if (game->inChat && (now - last_hb_time >= 15)) {
            char hb_topic[64];
            sprintf(hb_topic, "%s/Heartbeat/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
            mqtt_publish(hb_topic, game->clientID, false);
            last_hb_time = now;
            game->roomActivity[game->selectedCategoryIdx][game->selectedSubIdx] = now;
            
            bool found = false;
            int oldestIdx = 0;
            time_t oldestTime = now;
            for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                if(strcmp(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].clientID, game->clientID) == 0) {
                    game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].lastSeen = now;
                    found = true; break;
                }
                if(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].lastSeen < oldestTime) {
                    oldestTime = game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].lastSeen;
                    oldestIdx = u;
                }
            }
            if(!found) {
                snprintf(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].clientID, 32, "%s", game->clientID);
                game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].lastSeen = now;
            }
            game->needsRedrawTop = true;
        }
        
        renderTop();
        renderBottom();
        gspWaitForVBlank();
    }
    
    if (mqtt_sock >= 0) close(mqtt_sock);
    socExit();
    mcuHwcExit();
    free(SOC_buffer);
    if(game) free(game);
    C2D_TextBufDelete(g_dynBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}