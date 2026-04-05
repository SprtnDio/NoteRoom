#include <citro2d.h>
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/socket.h>
#include "constants.h"
#include "game.h"
#include "network.h"
#include "secrets.h"
#include "ui.h"
#include "drawing.h"

static u32 *SOC_buffer = NULL;

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();
    mcuHwcInit();
    psInit();
    u64 fc_seed = 0;
    PS_GetLocalFriendCodeSeed(&fc_seed);
    psExit();
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_dynBuf = C2D_TextBufNew(4096 * 8);
    SOC_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if(SOC_buffer) socInit(SOC_buffer, SOC_BUFFERSIZE);

    game = (GameState*)calloc(1, sizeof(GameState));
    if (!game) {
        printf("Out of memory!\n");
        return 0;
    }

    init_secrets();

    char ban_name[16];
    char time_name[16];
    char user_name[16];
    decrypt_string(ban_name, ENC_BAN_FILENAME, BAN_FILENAME_LEN, 0xAA);
    decrypt_string(time_name, ENC_TIME_FILENAME, TIME_FILENAME_LEN, 0xAA);
    decrypt_string(user_name, ENC_USER_FILENAME, USER_FILENAME_LEN, 0xAA);
    snprintf(ban_file_path, sizeof(ban_file_path), "sdmc:/3ds/NoteRoom/%s", ban_name);
    snprintf(time_file_path, sizeof(time_file_path), "sdmc:/3ds/NoteRoom/%s", time_name);
    snprintf(user_file_path, sizeof(user_file_path), "sdmc:/3ds/NoteRoom/%s", user_name);

    decrypt_string(admin_token, ENC_ADMIN_TOKEN, ADMIN_TOKEN_LEN, 0xAA);

    char banlist_suffix[32];
    decrypt_string(banlist_suffix, ENC_BANLIST_TOPIC, BANLIST_TOPIC_LEN, 0xAA);
    snprintf(admin_banlist_topic, sizeof(admin_banlist_topic), "%s/%s", g_base_topic, banlist_suffix);

    char suffix[32];
    decrypt_string(suffix, ENC_ADMIN_RESET_SUFFIX, ADMIN_RESET_SUFFIX_LEN, 0xAA);
    snprintf(admin_reset_topic, sizeof(admin_reset_topic), "%s/Admin/%s", g_base_topic, suffix);
    decrypt_string(suffix, ENC_ADMIN_UNBAN_SUFFIX, ADMIN_UNBAN_SUFFIX_LEN, 0xAA);
    snprintf(admin_unban_topic, sizeof(admin_unban_topic), "%s/Admin/%s", g_base_topic, suffix);
    decrypt_string(suffix, ENC_ADMIN_BAN_SUFFIX, ADMIN_BAN_SUFFIX_LEN, 0xAA);
    snprintf(admin_ban_topic, sizeof(admin_ban_topic), "%s/Admin/%s", g_base_topic, suffix);
    decrypt_string(suffix, ENC_ADMIN_KICK_SUFFIX, ADMIN_KICK_SUFFIX_LEN, 0xAA);
    snprintf(admin_kick_topic, sizeof(admin_kick_topic), "%s/Admin/%s", g_base_topic, suffix);
    decrypt_string(suffix, ENC_ADMIN_LIST_REQ_SUFFIX, ADMIN_LIST_REQ_SUFFIX_LEN, 0xAA);
    snprintf(admin_list_req_topic, sizeof(admin_list_req_topic), "%s/Admin/%s", g_base_topic, suffix);
    snprintf(admin_list_res_topic, sizeof(admin_list_res_topic), "%s/Admin/ListResponse", g_base_topic);
    decrypt_string(suffix, ENC_ADMIN_STATUS_SUFFIX, ADMIN_STATUS_SUFFIX_LEN, 0xAA);
    snprintf(admin_status_topic, sizeof(admin_status_topic), "%s/Admin/%s", g_base_topic, suffix);
    decrypt_string(suffix, ENC_ADMIN_ANNOUNCE_SUFFIX, ADMIN_ANNOUNCE_SUFFIX_LEN, 0xAA);
    snprintf(admin_announce_topic, sizeof(admin_announce_topic), "%s/Admin/%s", g_base_topic, suffix);

    loadTrustedTime();
    loadBannedList();
    cleanExpiredBans();

    game->autoScrollEnabled = true;
    game->isSyncing = true;
    game->connectionFailed = false;
    game->selectedDrawingIdx = -1;
    game->currentColorIdx = 0;
    game->rainbowMode = false;
    game->lastSendTime = 0;
    game->lastNetworkActivity = 0;
    game->voteActive = false;
    game->showBanConfirm = false;
    banRequestSent = false;
    lastBanSyncRequest = 0;
    game->ntpSyncInProgress = false;
    game->lastNtpSyncTime = 0;
    snprintf(game->macAddress, sizeof(game->macAddress), "%012llX", fc_seed);

    game->penSizes[0] = 2.0f; game->penSizes[1] = 4.0f; game->penSizes[2] = 8.0f;
    game->eraserSizes[0] = 2.0f; game->eraserSizes[1] = 4.0f; game->eraserSizes[2] = 8.0f;
    game->currentPenSize = 3.0f;
    game->currentEraserSize = 6.0f;

    game->appState = STATE_MAIN_MENU;
    game->needsRedrawTop = true;
    game->needsRedrawBottom = true;

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_dynBuf);
    C2D_TargetClear(top, C2D_Color32(30, 30, 35, 255));
    C2D_SceneBegin(top);
    C2D_Text t_sync;
    C2D_TextParse(&t_sync, g_dynBuf, "Loading Interface...");
    C2D_DrawText(&t_sync, C2D_WithColor | C2D_AlignCenter, 200, 110, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
    C2D_TargetClear(bottom, C2D_Color32(35, 35, 40, 255));
    C2D_SceneBegin(bottom);
    C2D_Text t_title;
    C2D_TextParse(&t_title, g_dynBuf, "NoteRoom");
    C2D_DrawText(&t_title, C2D_WithColor | C2D_AlignCenter, 160, 110, 0.6f, 0.9f, 0.9f, C2D_Color32(255, 200, 80, 255));
    C3D_FrameEnd(0);

    loadOrAskUsername();
    snprintf(game->clientID, 32, "3ds_%d_%d", (int)osGetTime(), rand() % 1000);

    mqtt_connect();

    time_t last_hb_time = 0;
    u64 boot_time = osGetTime();
    u64 last_cleanup_time = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        u32 kUp = hidKeysUp();

        if (kDown & KEY_START) {
            if (mqtt_sock >= 0) {
                shutdown(mqtt_sock, SHUT_RDWR);
                svcSleepThread(100000000);
                close(mqtt_sock);
                mqtt_sock = -1;
            }
            break;
        }

        if (kDown & KEY_SELECT) {
            if (game->connectionFailed || mqtt_sock < 0) {
                game->connectionFailed = false;
                last_reconnect_time = 0;
                banRequestSent = false;
                updateStatus("Retrying...");
                game->needsRedrawTop = true;
                game->needsRedrawBottom = true;
            }
        }

        u64 currentTime = osGetTime();

        if (mqtt_sock >= 0) {
            if (!game->trustedTimeValid && !game->ntpSyncInProgress) {
                if (ntp_sync_time()) {
                    updateStatus("NTP sync successful");
                }
            } else if (game->trustedTimeValid && (currentTime - game->lastNtpSyncTime) >= NTP_SYNC_INTERVAL_MS) {
                if (!game->ntpSyncInProgress) {
                    if (ntp_sync_time()) {
                        game->lastNtpSyncTime = currentTime;
                        updateStatus("NTP re-sync OK");
                    }
                }
            }
        }

        if (currentTime - last_cleanup_time > 60000) {
            cleanExpiredBans();
            last_cleanup_time = currentTime;
        }

        if (game->inChat && isBanned(game->macAddress)) {
            game->inChat = false;
            game->appState = STATE_MAIN_MENU;
            char timeStr[16];
            getBanRemainingTime(timeStr, sizeof(timeStr), game->macAddress);
            char msg[64];
            snprintf(msg, sizeof(msg), "BANNED: %s", timeStr);
            updateStatus(msg);
            char hb_topic[64];
            snprintf(hb_topic, sizeof(hb_topic), "%s/Heartbeat/C%d/S%d",
                     g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
            char leave_payload[64];
            snprintf(leave_payload, sizeof(leave_payload), "!%s", game->clientID);
            mqtt_publish(hb_topic, leave_payload, false);
        }

        if (game->isSyncing && (currentTime - boot_time > 7000)) {
            game->isSyncing = false;
            game->needsRedrawTop = true;
            game->needsRedrawBottom = true;
        }

        if ((game->appState == STATE_MAIN_MENU || game->appState == STATE_SUB_MENU) &&
            (currentTime - game->lastTopUiUpdate) > 2000) {
            game->needsRedrawTop = true;
            game->needsRedrawBottom = true;
            game->lastTopUiUpdate = currentTime;
        }

        if (game && (currentTime - game->lastNetworkActivity) < 500) {
            game->needsRedrawTop = true;
            game->needsRedrawBottom = true;
        }

        if (game->appState == STATE_CHAT && game->voteActive) {
            static u64 lastVoteSec = 0;
            u64 currentSec = currentTime / 1000;
            if (currentSec != lastVoteSec) {
                lastVoteSec = currentSec;
                game->needsRedrawTop = true;
            }
        }

        if (game->appState == STATE_MAIN_MENU) {
            if (kDown & KEY_UP) { game->selectedCategoryIdx = (game->selectedCategoryIdx - 1 + CATEGORY_COUNT) % CATEGORY_COUNT; game->needsRedrawTop = true; game->needsRedrawBottom = true;}
            if (kDown & KEY_DOWN) { game->selectedCategoryIdx = (game->selectedCategoryIdx + 1) % CATEGORY_COUNT; game->needsRedrawTop = true; game->needsRedrawBottom = true;}
            if (kDown & KEY_A) {
                if (!game->isSyncing) {
                    if (isBanned(game->macAddress)) {
                        char timeStr[16];
                        getBanRemainingTime(timeStr, sizeof(timeStr), game->macAddress);
                        char msg[64];
                        snprintf(msg, sizeof(msg), "BANNED: %s", timeStr);
                        updateStatus(msg);
                    } else {
                        game->selectedSubIdx = 0;
                        game->appState = STATE_SUB_MENU;
                        game->needsRedrawTop = true;
                        game->needsRedrawBottom = true;
                    }
                }
            }
            if (kDown & KEY_X) { editUsername(); }
            if (kDown & KEY_Y) {
                game->userColorIdx = (game->userColorIdx + 1) % NUM_USER_COLORS;
                saveUserData();
                game->needsRedrawTop = true;
                game->needsRedrawBottom = true;
            }
        }
        else if (game->appState == STATE_SUB_MENU) {
            if (kDown & KEY_UP) { game->selectedSubIdx = (game->selectedSubIdx - 1 + SUB_ROOM_COUNTS[game->selectedCategoryIdx]) % SUB_ROOM_COUNTS[game->selectedCategoryIdx]; game->needsRedrawTop = true; game->needsRedrawBottom = true; }
            if (kDown & KEY_DOWN) { game->selectedSubIdx = (game->selectedSubIdx + 1) % SUB_ROOM_COUNTS[game->selectedCategoryIdx]; game->needsRedrawTop = true; game->needsRedrawBottom = true; }
            if (kDown & KEY_B) { game->appState = STATE_MAIN_MENU; game->needsRedrawTop = true; game->needsRedrawBottom = true; }
            if (kDown & KEY_A) {
                if (getActiveUserCount(game->selectedCategoryIdx, game->selectedSubIdx) >= LOBBY_MAX_USERS) {
                    updateStatus("Lobby is FULL!");
                } else {
                    cleanExpiredBans();
                    if (isBanned(game->macAddress)) {
                        char timeStr[16];
                        getBanRemainingTime(timeStr, sizeof(timeStr), game->macAddress);
                        char msg[64];
                        snprintf(msg, sizeof(msg), "BANNED: %s", timeStr);
                        updateStatus(msg);
                    } else {
                        game->appState = STATE_CHAT;
                        game->inChat = true;
                        char topic[64];
                        snprintf(topic, sizeof(topic), "%s/Review/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                        mqtt_subscribe(topic);
                        char topic_vote[64];
                        snprintf(topic_vote, sizeof(topic_vote), "%s/Vote/+/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                        mqtt_subscribe(topic_vote);
                        last_hb_time = 0;
                        game->voteActive = false;
                        game->showBanConfirm = false;
                        game->uiSelectedUserIdx = 0;
                        game->needsRedrawTop = true;
                        game->needsRedrawBottom = true;
                    }
                }
            }
        }
        else if (game->appState == STATE_CHAT) {
            if (kDown & KEY_B) {
                if (game->voteActive && !game->iHaveVoted) {
                    char castPayload[32];
                    snprintf(castPayload, sizeof(castPayload), "%s|0", game->macAddress);
                    char castTopic[64];
                    snprintf(castTopic, sizeof(castTopic), "%s/Vote/Cast/C%d/S%d",
                             g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                    mqtt_publish(castTopic, castPayload, false);
                    game->iHaveVoted = true;
                    game->needsRedrawTop = true;
                }
                else if (game->showBanConfirm) {
                    game->showBanConfirm = false;
                    game->needsRedrawTop = true;
                }
                else {
                    char hb_topic[64];
                    snprintf(hb_topic, sizeof(hb_topic), "%s/Heartbeat/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                    char leave_payload[64];
                    snprintf(leave_payload, sizeof(leave_payload), "!%s", game->clientID);
                    mqtt_publish(hb_topic, leave_payload, false);
                    char topic[64];
                    snprintf(topic, sizeof(topic), "%s/Review/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                    mqtt_unsubscribe(topic);
                    char topic_vote[64];
                    snprintf(topic_vote, sizeof(topic_vote), "%s/Vote/+/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                    mqtt_unsubscribe(topic_vote);
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
                    game->undoCount = 0;
                    game->redoCount = 0;
                    game->hasUnsavedDrawing = false;
                    game->selectedDrawingIdx = -1;
                    game->voteActive = false;
                    game->showBanConfirm = false;
                    game->inChat = false;
                    game->appState = STATE_SUB_MENU;
                    game->needsRedrawTop = true;
                    game->needsRedrawBottom = true;
                }
            }

            RoomChat* room = &game->rooms[game->selectedCategoryIdx][game->selectedSubIdx];
            circlePosition pos;
            hidCircleRead(&pos);
            if (abs(pos.dy) > 20) {
                room->messageScrollOffset -= (pos.dy / 8);
                room->autoScroll = false;
                if (room->messageScrollOffset < 0) room->messageScrollOffset = 0;
                game->needsRedrawTop = true;
                game->needsRedrawBottom = true;
            }

            if (game->voteActive) {
                if (currentTime > game->voteEndTime) {
                    if (strcmp(game->voteInitiatorMac, game->macAddress) == 0) {
                        if (game->voteYes > game->voteNo) {
                            char reqTopic[64];
                            snprintf(reqTopic, sizeof(reqTopic), "%s/Global/BanReq", g_base_topic);
                            char reqPayload[128];
                            snprintf(reqPayload, sizeof(reqPayload), "%s|%s|VOTE_KICK_REQUEST", game->voteTargetMac, game->voteTargetName);
                            mqtt_publish(reqTopic, reqPayload, false);
                            updateStatus("Vote success - Request sent to Admin!");
                        } else {
                            updateStatus("Vote failed - Not enough votes!");
                        }
                    } else {
                        updateStatus("Waiting for vote result...");
                    }
                    game->voteActive = false;
                    game->needsRedrawTop = true;
                } else if (!game->iHaveVoted) {
                    if ((kDown & KEY_A)) {
                        int voteVal = 1;
                        char castPayload[32];
                        snprintf(castPayload, sizeof(castPayload), "%s|%d", game->macAddress, voteVal);
                        char castTopic[64];
                        snprintf(castTopic, sizeof(castTopic), "%s/Vote/Cast/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                        mqtt_publish(castTopic, castPayload, false);
                        game->iHaveVoted = true;
                        game->needsRedrawTop = true;
                    } else if (kDown & KEY_B) {
                        char castPayload[32];
                        snprintf(castPayload, sizeof(castPayload), "%s|0", game->macAddress);
                        char castTopic[64];
                        snprintf(castTopic, sizeof(castTopic), "%s/Vote/Cast/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                        mqtt_publish(castTopic, castPayload, false);
                        game->iHaveVoted = true;
                        game->needsRedrawTop = true;
                    }
                }
            }
            else if (game->showBanConfirm) {
                if (kDown & KEY_A) {
                    char startPayload[128];
                    snprintf(startPayload, sizeof(startPayload), "%s|%s|%s|%s",
                             game->voteTargetMac, game->voteTargetName,
                             game->macAddress, game->userName);
                    char startTopic[64];
                    snprintf(startTopic, sizeof(startTopic), "%s/Vote/Start/C%d/S%d",
                             g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
                    mqtt_publish(startTopic, startPayload, false);
                    game->voteActive = true;
                    snprintf(game->voteInitiatorMac, sizeof(game->voteInitiatorMac), "%s", game->macAddress);
                    snprintf(game->voteInitiatorName, sizeof(game->voteInitiatorName), "%s", game->userName);
                    game->voteEndTime = osGetTime() + 30000;
                    game->voteYes = 0;
                    game->voteNo = 0;
                    game->votedCount = 0;
                    game->iHaveVoted = false;
                    game->showBanConfirm = false;
                    game->needsRedrawTop = true;
                }
            }
            else {
                int activeCount = getActiveUserCount(game->selectedCategoryIdx, game->selectedSubIdx);
                if (activeCount > 0) {
                    if (kDown & KEY_DLEFT) {
                        game->uiSelectedUserIdx = (game->uiSelectedUserIdx - 1 + activeCount) % activeCount;
                        game->needsRedrawTop = true;
                    }
                    if (kDown & KEY_DRIGHT) {
                        game->uiSelectedUserIdx = (game->uiSelectedUserIdx + 1) % activeCount;
                        game->needsRedrawTop = true;
                    }
                    if (kDown & KEY_A) {
                        ActiveUser* target = getActiveUserByIndex(game->selectedCategoryIdx, game->selectedSubIdx, game->uiSelectedUserIdx);
                        if (target && strcmp(target->mac, game->macAddress) != 0 && strlen(target->mac) > 0) {
                            if (isBanned(target->mac)) {
                                updateStatus("User already banned!");
                            } else {
                                game->showBanConfirm = true;
                                snprintf(game->voteTargetMac, sizeof(game->voteTargetMac), "%s", target->mac);
                                snprintf(game->voteTargetName, sizeof(game->voteTargetName), "%s", target->name);
                                game->needsRedrawTop = true;
                            }
                        } else if (target && strcmp(target->mac, game->macAddress) == 0) {
                            updateStatus("You cannot vote yourself!");
                        }
                    }
                }
            }

            if (!game->isDrawing) {
                if (kDown & KEY_DUP) {
                    int idx = game->selectedDrawingIdx;
                    if (idx < 0) idx = room->messageCount;
                    for (int i = idx - 1; i >= 0; i--) {
                        if (room->messages[i].isDrawing && room->messages[i].drawingData) {
                            if (room->messages[i].drawingCount <= MAX_INK_LIMIT) {
                                loadDrawingFromMessage(&room->messages[i]);
                                game->selectedDrawingIdx = i;
                            } else {
                                updateStatus("Drawing too large!");
                            }
                            break;
                        }
                    }
                    game->needsRedrawBottom = true;
                    game->needsRedrawTop = true;
                }
                if (kDown & KEY_DDOWN) {
                    int start = (game->selectedDrawingIdx < 0) ? 0 : game->selectedDrawingIdx + 1;
                    for (int i = start; i < room->messageCount; i++) {
                        if (room->messages[i].isDrawing && room->messages[i].drawingData) {
                            if (room->messages[i].drawingCount <= MAX_INK_LIMIT) {
                                loadDrawingFromMessage(&room->messages[i]);
                                game->selectedDrawingIdx = i;
                            } else {
                                updateStatus("Drawing too large!");
                            }
                            break;
                        }
                    }
                    game->needsRedrawBottom = true;
                    game->needsRedrawTop = true;
                }
            }

            if (kHeld & KEY_TOUCH) {
                touchPosition touch;
                hidTouchRead(&touch);
                if (touch.py >= SECOND_BAR_Y && touch.py < TOOLBAR_Y_START) {
                    if (touch.px >= 64 && touch.px < 256) {
                        float val = (float)(touch.px - 90) / 140.0f;
                        if (val < 0.0f) val = 0.0f;
                        if (val > 1.0f) val = 1.0f;
                        float newSize = 1.0f + val * 9.0f;
                        if (game->isEraser) {
                            game->currentEraserSize = newSize;
                        } else {
                            game->currentPenSize = newSize;
                        }
                        game->needsRedrawBottom = true;
                    }
                }
                if (touch.py >= DRAWING_AREA_TOP && touch.py < DRAWING_AREA_BOTTOM) {
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
                    if (touch.px < 64) {
                        saveUndoState();
                        game->userDrawingCount = 0;
                        game->userStrokeCount = 0;
                        game->selectedDrawingIdx = -1;
                        game->needsRedrawBottom = true;
                    }
                    else if (touch.px < 128) { game->isEraser = false; game->needsRedrawBottom = true; }
                    else if (touch.px < 192) { game->isEraser = true; game->needsRedrawBottom = true; }
                    else if (touch.px < 256) { openKeyboard(); }
                    else { sendDrawing(); }
                }
                else if (touch.py >= COLOR_BAR_Y && touch.py < COLOR_BAR_Y + COLOR_BTN_SIZE) {
                    int idx = (touch.px - COLOR_BTN_START_X) / (COLOR_BTN_SIZE + COLOR_BTN_SPACING);
                    if (idx >= 0 && idx < NUM_USER_COLORS) {
                        int x_start = COLOR_BTN_START_X + idx * (COLOR_BTN_SIZE + COLOR_BTN_SPACING);
                        if (touch.px >= x_start && touch.px < x_start + COLOR_BTN_SIZE) {
                            game->currentColorIdx = idx;
                            game->rainbowMode = false;
                            game->needsRedrawBottom = true;
                        }
                    } else if (idx == NUM_USER_COLORS) {
                        int x_start = COLOR_BTN_START_X + NUM_USER_COLORS * (COLOR_BTN_SIZE + COLOR_BTN_SPACING);
                        if (touch.px >= x_start && touch.px < x_start + COLOR_BTN_SIZE) {
                            game->rainbowMode = !game->rainbowMode;
                            game->needsRedrawBottom = true;
                        }
                    }
                }
                else if (touch.py >= SECOND_BAR_Y && touch.py < TOOLBAR_Y_START) {
                    if (touch.px < 64) {
                        if (touch.py < SECOND_BAR_Y + 13) {
                            int idx = game->selectedDrawingIdx;
                            if (idx < 0) idx = room->messageCount;
                            for (int i = idx - 1; i >= 0; i--) {
                                if (room->messages[i].isDrawing && room->messages[i].drawingData) {
                                    if (room->messages[i].drawingCount <= MAX_INK_LIMIT) {
                                        loadDrawingFromMessage(&room->messages[i]);
                                        game->selectedDrawingIdx = i;
                                    }
                                    break;
                                }
                            }
                        } else {
                            int start = (game->selectedDrawingIdx < 0) ? 0 : game->selectedDrawingIdx + 1;
                            for (int i = start; i < room->messageCount; i++) {
                                if (room->messages[i].isDrawing && room->messages[i].drawingData) {
                                    if (room->messages[i].drawingCount <= MAX_INK_LIMIT) {
                                        loadDrawingFromMessage(&room->messages[i]);
                                        game->selectedDrawingIdx = i;
                                    }
                                    break;
                                }
                            }
                        }
                        game->needsRedrawBottom = true;
                        game->needsRedrawTop = true;
                    }
                    else if (touch.px >= 256) {
                        game->autoScrollEnabled = !game->autoScrollEnabled;
                        game->needsRedrawBottom = true;
                    }
                    else {
                        float val = (float)(touch.px - 90) / 140.0f;
                        if (val < 0.0f) val = 0.0f;
                        if (val > 1.0f) val = 1.0f;
                        float newSize = 1.0f + val * 9.0f;
                        if (game->isEraser) {
                            game->currentEraserSize = newSize;
                        } else {
                            game->currentPenSize = newSize;
                        }
                        game->needsRedrawBottom = true;
                    }
                }
            }

            if (kDown & KEY_L) {
                if (game->undoCount > 0) {
                    if (game->redoCount < MAX_UNDO_STEPS) {
                        saveSnapshot(&game->redoSnapshots[game->redoCount], game->userDrawing, game->userDrawingCount,
                                     game->userStrokeStarts, game->userStrokeCount);
                        game->redoCount++;
                    }
                    game->undoCount--;
                    loadSnapshot(&game->undoSnapshots[game->undoCount], game->userDrawing, &game->userDrawingCount,
                                 game->userStrokeStarts, &game->userStrokeCount);
                    game->needsRedrawBottom = true;
                }
            }
            if (kDown & KEY_R) {
                if (game->redoCount > 0) {
                    if (game->undoCount < MAX_UNDO_STEPS) {
                        saveSnapshot(&game->undoSnapshots[game->undoCount], game->userDrawing, game->userDrawingCount,
                                     game->userStrokeStarts, game->userStrokeCount);
                        game->undoCount++;
                    }
                    game->redoCount--;
                    loadSnapshot(&game->redoSnapshots[game->redoCount], game->userDrawing, &game->userDrawingCount,
                                 game->userStrokeStarts, &game->userStrokeCount);
                    game->needsRedrawBottom = true;
                }
            }
            if (kDown & KEY_Y) {
                game->autoScrollEnabled = !game->autoScrollEnabled;
                game->needsRedrawBottom = true;
            }
        }

        mqtt_poll();
        mqtt_ping();

        time_t now = time(NULL);
        if (game->inChat && (now - last_hb_time >= 15)) {
            char hb_topic[64];
            snprintf(hb_topic, sizeof(hb_topic), "%s/Heartbeat/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
            char hb_payload[128];
            snprintf(hb_payload, sizeof(hb_payload), "%s|%s|%s", game->clientID, game->userName, game->macAddress);
            mqtt_publish(hb_topic, hb_payload, false);
            last_hb_time = now;
            time_t trustedNow = getTrustedTime();
            if (trustedNow > 0) game->roomActivity[game->selectedCategoryIdx][game->selectedSubIdx] = trustedNow;
            bool found = false;
            int oldestIdx = 0;
            time_t oldestTime = trustedNow;
            for(int u = 0; u < MAX_ACTIVE_USERS; u++) {
                if(strcmp(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].clientID, game->clientID) == 0) {
                    game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].lastSeen = trustedNow;
                    found = true; break;
                }
                if(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].lastSeen < oldestTime) {
                    oldestTime = game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][u].lastSeen;
                    oldestIdx = u;
                }
            }
            if(!found && trustedNow > 0) {
                snprintf(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].clientID, sizeof(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].clientID), "%s", game->clientID);
                snprintf(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].name, sizeof(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].name), "%s", game->userName);
                snprintf(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].mac, sizeof(game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].mac), "%s", game->macAddress);
                game->activeUsers[game->selectedCategoryIdx][game->selectedSubIdx][oldestIdx].lastSeen = trustedNow;
            }
            game->needsRedrawTop = true;
            game->needsRedrawBottom = true;
        }

        if (game->needsRedrawTop || game->needsRedrawBottom) {
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TextBufClear(g_dynBuf);
            renderTop();
            renderBottom();
            C3D_FrameEnd(0);
            game->needsRedrawTop = false;
            game->needsRedrawBottom = false;
        }
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