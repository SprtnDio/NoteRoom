#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <citro2d.h>
#include "ui.h"
#include "drawing.h"
#include "network.h"
#include "secrets.h"

C3D_RenderTarget *top, *bottom;
C2D_TextBuf g_dynBuf;

const u32 USER_COLORS[NUM_USER_COLORS] = {
    0xFFFFFFFF, 0xFF808080, 0xFFFFAA64, 0xFF6464FF,
    0xFF64FF64, 0xFF64FFFF, 0xFFFF96FF, 0xFF00A5FF
};

u32 rainbowColor(float pos) {
    float r = 0, g = 0, b = 0;
    int hue = (int)(pos * 360) % 360;
    float s = 1.0f, v = 1.0f;
    int hi = (hue / 60) % 6;
    float f = (hue / 60.0f) - hi;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);
    switch (hi) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
    }
    return C2D_Color32((int)(r*255), (int)(g*255), (int)(b*255), 255);
}

u32 getPointColor(Point p) {
    if (p.color == NUM_USER_COLORS) {
        return rainbowColor(p.x / 320.0f);
    } else if (p.color < NUM_USER_COLORS) {
        return USER_COLORS[p.color];
    }
    return USER_COLORS[0];
}

void updateStatus(const char* t, u32 color) {
    if (game) {
        snprintf(game->statusMsg, sizeof(game->statusMsg), "%s", t);
        game->statusColor = color;
        game->statusMsgTimer = osGetTime() + 2500;
        game->needsRedrawTop = true;
        game->needsRedrawBottom = true;
        game->lastNetworkActivity = osGetTime();
    }
}

void renderSnapshotPreview(DrawingSnapshot* snap, float startX, float startY, float width, float height) {
    if (!snap || snap->pointCount < 2) return;
    float canvasW = 320.0f;
    float canvasH = (float)(DRAWING_AREA_BOTTOM - DRAWING_AREA_TOP);
    float scaleX = width / canvasW;
    float scaleY = height / canvasH;
    float scale = (scaleX < scaleY) ? scaleX : scaleY; 
    float offsetX = startX + (width - (canvasW * scale)) / 2.0f;
    float offsetY = startY + (height - (canvasH * scale)) / 2.0f;
    
    for (int j = 1; j < snap->pointCount; j++) {
        Point p1 = snap->points[j-1];
        Point p2 = snap->points[j];
        if (p1.x != 0xFFFF && p2.x != 0xFFFF) {
            float displaySize = p2.thickness * scale;
            if (displaySize < 1.0f) displaySize = 1.0f;
            u32 col = (p2.type == 1) ? C2D_Color32(50, 50, 55, 255) : getPointColor(p2); 
            float x1 = offsetX + (float)p1.x * scale;
            float y1 = offsetY + (float)(p1.y - DRAWING_AREA_TOP) * scale;
            float x2 = offsetX + (float)p2.x * scale;
            float y2 = offsetY + (float)(p2.y - DRAWING_AREA_TOP) * scale;
            C2D_DrawLine(x1, y1, col, x2, y2, col, displaySize, 0.5f);
        }
    }
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
            float displaySize = p2.thickness * scale;
            if (displaySize < 1.0f) displaySize = 1.0f;
            u32 col = (p2.type == 1) ? C2D_Color32(50, 55, 70, 255) : getPointColor(p2);
            float x1 = offsetX + (float)p1.x * scale;
            float y1 = offsetY + (float)(p1.y - DRAWING_AREA_TOP) * scale;
            float x2 = offsetX + (float)p2.x * scale;
            float y2 = offsetY + (float)(p2.y - DRAWING_AREA_TOP) * scale;
            C2D_DrawLine(x1, y1, col, x2, y2, col, displaySize, 0.5f);
            if (displaySize >= 1.5f) {
                if (j == 1 || msg->drawingData[j-2].x == 0xFFFF) {
                    C2D_DrawCircleSolid(x1, y1, 0.5f, displaySize / 2.0f, col);
                }
                if (j == msg->drawingCount - 1 || msg->drawingData[j+1].x == 0xFFFF) {
                    C2D_DrawCircleSolid(x2, y2, 0.5f, displaySize / 2.0f, col);
                }
            }
        }
    }
}

void drawTextBubble(float x, float y, float width, float height, ChatMessage* msg) {
    C2D_DrawRectSolid(x, y, 0.5f, width, height, C2D_Color32(50, 55, 70, 255));
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x + width, y, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y + height, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x + width, y, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_Text t_sender;
    C2D_TextParse(&t_sender, g_dynBuf, msg->sender);
    u32 nameColor = (msg->nameColorIdx < NUM_USER_COLORS) ? USER_COLORS[msg->nameColorIdx] : USER_COLORS[0];
    C2D_DrawText(&t_sender, C2D_WithColor | C2D_AlignLeft, x + 4, y + 4, 0.5f, 0.5f, 0.5f, nameColor);
    
    if (isAdminMac(msg->senderMac)) {
        C2D_Text t_admin;
        C2D_TextParse(&t_admin, g_dynBuf, "[ADMIN]");
        C2D_DrawText(&t_admin, C2D_WithColor | C2D_AlignRight, x + width - 4, y + 4, 0.5f, 0.5f, 0.5f, C2D_Color32(255, 215, 0, 255));
    }
    
    int textOffset = 0;
    float lineY = y + 22;
    float textHeight = 18.0f;
    int text_len = strlen(msg->text);
    for (int line = 0; line < msg->wrappedLines && textOffset < text_len; line++) {
        int charsThisLine = CHARS_PER_LINE;
        if (textOffset + charsThisLine >= text_len) {
            charsThisLine = text_len - textOffset;
        } else {
            int space_idx = -1;
            for(int j = textOffset + charsThisLine; j > textOffset; j--) {
                if(msg->text[j] == ' ') { space_idx = j; break; }
            }
            if(space_idx != -1) {
                charsThisLine = space_idx - textOffset;
            }
        }
        char lineText[CHARS_PER_LINE + 2] = {0};
        snprintf(lineText, sizeof(lineText), "%.*s", charsThisLine, msg->text + textOffset);
        C2D_Text t_content;
        C2D_TextParse(&t_content, g_dynBuf, lineText);
        C2D_DrawText(&t_content, C2D_WithColor, x + 4, lineY, 0.5f, 0.55f, 0.55f, C2D_Color32(255, 255, 255, 255));
        textOffset += charsThisLine;
        if (textOffset < text_len && msg->text[textOffset] == ' ') {
            textOffset++;
        }
        lineY += textHeight;
    }
}

void drawDrawingBubble(float x, float y, float width, float height, ChatMessage* msg) {
    C2D_DrawRectSolid(x, y, 0.5f, width, height, C2D_Color32(50, 55, 70, 255));
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x + width, y, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y + height, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x, y, C2D_Color32(80, 80, 100, 255), x, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_DrawLine(x + width, y, C2D_Color32(80, 80, 100, 255), x + width, y + height, C2D_Color32(80, 80, 100, 255), 2.0f, 0.5f);
    C2D_Text t_sender;
    C2D_TextParse(&t_sender, g_dynBuf, msg->sender);
    u32 nameColor = (msg->nameColorIdx < NUM_USER_COLORS) ? USER_COLORS[msg->nameColorIdx] : USER_COLORS[0];
    C2D_DrawText(&t_sender, C2D_WithColor | C2D_AlignLeft, x + 4, y + 4, 0.5f, 0.5f, 0.5f, nameColor);
    
    if (isAdminMac(msg->senderMac)) {
        C2D_Text t_admin;
        C2D_TextParse(&t_admin, g_dynBuf, "[ADMIN]");
        C2D_DrawText(&t_admin, C2D_WithColor | C2D_AlignRight, x + width - 4, y + 4, 0.5f, 0.5f, 0.5f, C2D_Color32(255, 215, 0, 255));
    }
    
    renderDrawingPreview(msg, x + 4, y + 22, width - 8, height - 26);
}

void renderScrollBar(float totalHeight, float visibleHeight, float scrollOffset) {
    if (totalHeight <= visibleHeight) return;
    float startY = 42;
    float availableHeight = DRAWING_AREA_HEIGHT - startY - 10;
    float barHeight = (visibleHeight / totalHeight) * availableHeight;
    if (barHeight < 20) barHeight = 20;
    if (barHeight > availableHeight) barHeight = availableHeight;
    float maxScroll = totalHeight - visibleHeight;
    float scrollPercent = (maxScroll > 0) ? (scrollOffset / maxScroll) : 0;
    if (scrollPercent > 1.0f) scrollPercent = 1.0f;
    float barY = startY + 5 + scrollPercent * (availableHeight - barHeight);
    C2D_DrawRectSolid(SCROLL_BAR_X, startY, 0.5f, SCROLL_BAR_WIDTH, DRAWING_AREA_HEIGHT - startY, C2D_Color32(60, 60, 60, 255));
    C2D_DrawRectSolid(SCROLL_BAR_X + 2, barY, 0.5f, SCROLL_BAR_WIDTH - 4, barHeight, C2D_Color32(100, 100, 100, 255));
}

void drawActivityDot(float x, float y, int userCount) {
    u32 color = (userCount > 0) ? C2D_Color32(0, 255, 100, 255) : C2D_Color32(255, 255, 255, 255);
    C2D_DrawCircleSolid(x, y, 0.5f, 5.0f, color);
}

void drawDrawingToolbar() {
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 25, C2D_Color32(40, 40, 45, 255));
    C2D_Text t_undo;
    C2D_TextParse(&t_undo, g_dynBuf, "L=Undo");
    C2D_DrawText(&t_undo, C2D_WithColor, 5, 6, 0.5f, 0.6f, 0.6f, C2D_Color32(200, 200, 200, 255));
    C2D_Text t_redo;
    C2D_TextParse(&t_redo, g_dynBuf, "R=Redo");
    C2D_DrawText(&t_redo, C2D_WithColor | C2D_AlignRight, 315, 6, 0.5f, 0.6f, 0.6f, C2D_Color32(200, 200, 200, 255));
    
    char xStr[32];
    float currentSize = game->isEraser ? game->currentEraserSize : game->currentPenSize;
    snprintf(xStr, sizeof(xStr), "Size=%.1fpx", currentSize);
    C2D_Text t_x;
    C2D_TextParse(&t_x, g_dynBuf, xStr);
    C2D_DrawText(&t_x, C2D_WithColor | C2D_AlignCenter, 160, 6, 0.5f, 0.6f, 0.6f, C2D_Color32(100, 180, 255, 255));
}

void sendTextMessage(const char* text) {
    if (sendInProgress) return;
    if (game->isSyncing || isBanned(game->macAddress)) return;

    if (!text || strlen(text) == 0) return;
    u64 now = osGetTime();
    if (now - game->lastSendTime < 5000) {
        updateStatus("Wait 5 seconds to send!", C2D_Color32(200, 150, 50, 255));
        return;
    }
    sendInProgress = true;
    int payloadSize = strlen(game->userName) + 30 + strlen(text) + 1;
    char* payload = (char*)malloc(payloadSize);
    if (payload) {
        snprintf(payload, payloadSize, "%s#%d#%s|TEXT:%s", game->userName, game->userColorIdx, game->macAddress, text);
        char topic[64];
        snprintf(topic, sizeof(topic), "%s/Review/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
        mqtt_publish(topic, payload, false);
        free(payload);
        addMessage(game->userName, game->userColorIdx, text, false, NULL, 0, NULL, 0, game->macAddress);
        game->lastSendTime = now;
        updateStatus("Sent!", C2D_Color32(50, 100, 255, 255));
    }
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
    game->needsRedrawTop = true;
}

void renderTop() {
    C2D_TargetClear(top, C2D_Color32(30, 30, 35, 255));
    C2D_SceneBegin(top);
    u8 battery_raw = 0;
    MCUHWC_GetBatteryLevel(&battery_raw);
    if (battery_raw > 100) battery_raw = 100;
    u8 wifi = osGetWifiStrength();

    if (game->appState == STATE_MAIN_MENU || game->appState == STATE_SUB_MENU) {
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 25, C2D_Color32(40, 45, 55, 255));
        C2D_DrawLine(0, 25, C2D_Color32(100, 180, 255, 255), 400, 25, C2D_Color32(100, 180, 255, 255), 2.0f, 0.5f);
        if (game->appState == STATE_MAIN_MENU) {
            C2D_Text t_title_name;
            C2D_TextParse(&t_title_name, g_dynBuf, game->userName);
            u32 nameColor = (game->userColorIdx < NUM_USER_COLORS) ? USER_COLORS[game->userColorIdx] : USER_COLORS[0];
            C2D_DrawText(&t_title_name, C2D_WithColor | C2D_AlignLeft, 5, 4, 0.5f, 0.6f, 0.6f, nameColor);
        } else {
            char titleStr[64];
            snprintf(titleStr, sizeof(titleStr), "%s", ROOM_NAMES[game->selectedCategoryIdx]);
            C2D_Text t_title;
            C2D_TextParse(&t_title, g_dynBuf, titleStr);
            C2D_DrawText(&t_title, C2D_WithColor | C2D_AlignLeft, 5, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        }
        if (game->isSyncing) {
            C2D_Text t_sync;
            if (isBanned(game->macAddress)) {
                C2D_TextParse(&t_sync, g_dynBuf, "BANNED (Locked)");
                C2D_DrawText(&t_sync, C2D_WithColor | C2D_AlignCenter, 200, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 50, 50, 255));
            } else {
                C2D_TextParse(&t_sync, g_dynBuf, "Syncing...");
                C2D_DrawText(&t_sync, C2D_WithColor | C2D_AlignCenter, 200, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(180, 180, 180, 255));
            }
        } else if (game->statusMsgTimer > osGetTime()) {
            C2D_DrawRectSolid(0, 0, 0.5f, 400, 25, game->statusColor);
            u32 lighterColor = C2D_Color32(
                (game->statusColor & 0xFF) + 50 > 255 ? 255 : (game->statusColor & 0xFF) + 50,
                ((game->statusColor >> 8) & 0xFF) + 50 > 255 ? 255 : ((game->statusColor >> 8) & 0xFF) + 50,
                ((game->statusColor >> 16) & 0xFF) + 50 > 255 ? 255 : ((game->statusColor >> 16) & 0xFF) + 50,
                255
            );
            C2D_DrawLine(0, 25, lighterColor, 400, 25, lighterColor, 2.0f, 0.5f);
            C2D_Text t_stat;
            C2D_TextParse(&t_stat, g_dynBuf, game->statusMsg);
            C2D_DrawText(&t_stat, C2D_WithColor | C2D_AlignCenter, 200, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        } else {
            u32 connCol = (mqtt_sock >= 0) ? C2D_Color32(0, 255, 100, 255) : C2D_Color32(255, 255, 255, 255);
            const char* connText = (mqtt_sock >= 0) ? "Online" : "Offline";
            C2D_Text t_conn;
            C2D_TextParse(&t_conn, g_dynBuf, connText);
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
        char sysInfo[32];
        snprintf(sysInfo, sizeof(sysInfo), "Bat:%d%%", battery_raw);
        C2D_Text t_sys;
        C2D_TextParse(&t_sys, g_dynBuf, sysInfo);
        C2D_DrawText(&t_sys, C2D_WithColor | C2D_AlignRight, 395, 5, 0.5f, 0.6f, 0.6f, C2D_Color32(180, 180, 200, 255));

        int roomWidth = 340, roomHeight = 22, startX = (400 - roomWidth) / 2, startY = 38, spacing = 3;
        int itemCount = (game->appState == STATE_MAIN_MENU) ? CATEGORY_COUNT : SUB_ROOM_COUNTS[game->selectedCategoryIdx];
        int selectedItem = (game->appState == STATE_MAIN_MENU) ? game->selectedCategoryIdx : game->selectedSubIdx;
        for(int i = 0; i < itemCount; i++) {
            int ry = startY + i * (roomHeight + spacing);
            u32 bgColor = (i == selectedItem) ? C2D_Color32(70, 100, 140, 255) : C2D_Color32(50, 55, 65, 255);
            C2D_DrawRectSolid(startX, ry, 0.5f, roomWidth, roomHeight, bgColor);
            const char* itemName = (game->appState == STATE_MAIN_MENU) ? ROOM_NAMES[i] : SUB_ROOM_NAMES[game->selectedCategoryIdx][i];
            C2D_Text t_lobby;
            C2D_TextParse(&t_lobby, g_dynBuf, itemName);
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
            C2D_Text t_users;
            C2D_TextParse(&t_users, g_dynBuf, uCountStr);
            C2D_DrawText(&t_users, C2D_WithColor | C2D_AlignRight, startX + roomWidth - 30, ry + 5, 0.55f, 0.5f, 0.5f, C2D_Color32(180, 200, 255, 255));
            drawActivityDot(startX + roomWidth - 15, ry + 11, totalUsers);
        }
        const char* inst;
        u32 instCol = C2D_Color32(150, 150, 170, 255);
        if (game->appState == STATE_MAIN_MENU) {
            if (game->isSyncing || isBanned(game->macAddress)) {
                inst = "[ ] Locked...   [X] Name[Y] Color   [START] Exit";
                instCol = C2D_Color32(100, 100, 100, 255);
            } else {
                inst = "[A] Select   [X] Name[Y] Color[START] Exit";
            }
        } else {
            inst = "[A] Join   [B] Back[START] Exit";
        }
        C2D_Text t_ins;
        C2D_TextParse(&t_ins, g_dynBuf, inst);
        C2D_DrawText(&t_ins, C2D_WithColor | C2D_AlignCenter, 200, 218, 0.5f, 0.55f, 0.55f, instCol);
    }
    else if (game->appState == STATE_CHAT || game->appState == STATE_SAVE_MENU || game->appState == STATE_LOAD_MENU) {
        C2D_TargetClear(top, C2D_Color32(25, 25, 30, 255));
        C2D_SceneBegin(top);
        RoomChat* room = &game->rooms[game->selectedCategoryIdx][game->selectedSubIdx];
        float chatStartY = 42;
        if (room->messageCount > 0) {
            float textHeight = 18.0f, drawingHeight = DRAWING_PREVIEW_HEIGHT + 25;
            float totalMsgHeight = 0;
            for (int i = 0; i < room->messageCount; i++) {
                totalMsgHeight += (room->messages[i].isDrawing ? drawingHeight : (textHeight * room->messages[i].wrappedLines + 25)) + 2;
            }
            int maxS = (int)(totalMsgHeight - (DRAWING_AREA_HEIGHT - chatStartY) + MESSAGE_BOTTOM_PADDING);
            if (maxS < 0) maxS = 0;
            if (room->autoScroll) { room->messageScrollOffset = maxS; room->autoScroll = false; }
            else {
                if (room->messageScrollOffset > maxS) room->messageScrollOffset = maxS;
                if (room->messageScrollOffset < 0) room->messageScrollOffset = 0;
            }
            float msgY = chatStartY + 2 - room->messageScrollOffset;
            for (int i = 0; i < room->messageCount; i++) {
                ChatMessage* msg = &room->messages[i];
                if (!msg) continue;
                float currentItemHeight = msg->isDrawing ? drawingHeight : (textHeight * msg->wrappedLines + 25);
                if (msgY + currentItemHeight > chatStartY && msgY < DRAWING_AREA_HEIGHT) {
                    if (msg->isDrawing) {
                        drawDrawingBubble(20, msgY, 360, drawingHeight, msg);
                    } else {
                        drawTextBubble(20, msgY, 360, textHeight * msg->wrappedLines + 25, msg);
                    }
                }
                msgY += currentItemHeight + 2;
            }
            renderScrollBar(totalMsgHeight, DRAWING_AREA_HEIGHT - chatStartY, room->messageScrollOffset);
        } else {
            C2D_Text t_wait;
            C2D_TextParse(&t_wait, g_dynBuf, "Waiting for messages...");
            C2D_DrawText(&t_wait, C2D_WithColor | C2D_AlignCenter, 200, 110, 0.5f, 0.6f, 0.6f, C2D_Color32(100, 100, 100, 255));
        }
        int activeCount = getActiveUserCount(game->selectedCategoryIdx, game->selectedSubIdx);
        if (game->statusMsgTimer > osGetTime()) {
            C2D_DrawRectSolid(0, 0, 0.5f, 400, 25, game->statusColor);
            u32 lighterColor = C2D_Color32(
                (game->statusColor & 0xFF) + 50 > 255 ? 255 : (game->statusColor & 0xFF) + 50,
                ((game->statusColor >> 8) & 0xFF) + 50 > 255 ? 255 : ((game->statusColor >> 8) & 0xFF) + 50,
                ((game->statusColor >> 16) & 0xFF) + 50 > 255 ? 255 : ((game->statusColor >> 16) & 0xFF) + 50,
                255
            );
            C2D_DrawLine(0, 25, lighterColor, 400, 25, lighterColor, 2.0f, 0.5f);
            C2D_Text t_stat;
            C2D_TextParse(&t_stat, g_dynBuf, game->statusMsg);
            C2D_DrawText(&t_stat, C2D_WithColor | C2D_AlignCenter, 200, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        } else {
            C2D_DrawRectSolid(0, 0, 0.5f, 400, 25, C2D_Color32(40, 45, 55, 255));
            C2D_DrawLine(0, 25, C2D_Color32(100, 180, 255, 255), 400, 25, C2D_Color32(100, 180, 255, 255), 2.0f, 0.5f);
            char header_title[64];
            snprintf(header_title, sizeof(header_title), "%s - %s   ", ROOM_NAMES[game->selectedCategoryIdx], SUB_ROOM_NAMES[game->selectedCategoryIdx][game->selectedSubIdx]);
            C2D_Text t_lobby_ind;
            C2D_TextParse(&t_lobby_ind, g_dynBuf, header_title);
            C2D_DrawText(&t_lobby_ind, C2D_WithColor | C2D_AlignLeft, 5, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
            float tw, th;
            C2D_TextGetDimensions(&t_lobby_ind, 0.6f, 0.6f, &tw, &th);
            drawActivityDot(5 + tw + 5, 12, activeCount);
            char header_num[16];
            snprintf(header_num, sizeof(header_num), "%d/%d", activeCount, LOBBY_MAX_USERS);
            C2D_Text t_num;
            C2D_TextParse(&t_num, g_dynBuf, header_num);
            C2D_DrawText(&t_num, C2D_WithColor | C2D_AlignLeft, 5 + tw + 15, 4, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
            float wifi_x = 310;
            float wifi_y = 17;
            u32 wCol = C2D_Color32(200, 200, 200, 255);
            C2D_DrawCircleSolid(wifi_x, wifi_y, 0.5f, 2.0f, (wifi >= 1) ? wCol : C2D_Color32(80, 80, 80, 255));
            C2D_DrawRectSolid(wifi_x + 4, wifi_y - 4, 0.5f, 3, 6, (wifi >= 2) ? wCol : C2D_Color32(80, 80, 80, 255));
            C2D_DrawRectSolid(wifi_x + 9, wifi_y - 8, 0.5f, 3, 10, (wifi >= 3) ? wCol : C2D_Color32(80, 80, 80, 255));
            char sysInfo[32];
            snprintf(sysInfo, sizeof(sysInfo), "Bat:%d%%", battery_raw);
            C2D_Text t_sys;
            C2D_TextParse(&t_sys, g_dynBuf, sysInfo);
            C2D_DrawText(&t_sys, C2D_WithColor | C2D_AlignRight, 395, 5, 0.5f, 0.6f, 0.6f, C2D_Color32(180, 180, 200, 255));
        }

        if (game->voteActive) {
            C2D_DrawRectSolid(20, 40, 0.8f, 360, 80, C2D_Color32(200, 50, 50, 200));
            C2D_DrawRectSolid(22, 42, 0.8f, 356, 76, C2D_Color32(40, 40, 45, 255));
            char title[64];
            snprintf(title, sizeof(title), "Vote Kick: %s ?", game->voteTargetName);
            C2D_Text t_vtitle;
            C2D_TextParse(&t_vtitle, g_dynBuf, title);
            C2D_DrawText(&t_vtitle, C2D_WithColor | C2D_AlignCenter, 200, 48, 0.8f, 0.6f, 0.6f, C2D_Color32(255, 100, 100, 255));
            char info[64];
            snprintf(info, sizeof(info), "Started by: %s   |   Yes: %d   No: %d", game->voteInitiatorName, game->voteYes, game->voteNo);
            C2D_Text t_vinfo;
            C2D_TextParse(&t_vinfo, g_dynBuf, info);
            C2D_DrawText(&t_vinfo, C2D_WithColor | C2D_AlignCenter, 200, 70, 0.8f, 0.5f, 0.5f, C2D_Color32(200, 200, 200, 255));
            u64 now_ms = osGetTime();
            int timeLeft = 0;
            if (game->voteEndTime > now_ms) timeLeft = (game->voteEndTime - now_ms) / 1000;
            char inst[64];
            if (game->iHaveVoted) snprintf(inst, sizeof(inst), "Waiting for result... (%ds)", timeLeft);
            else snprintf(inst, sizeof(inst), "[A] YES   [B] NO   (%ds)", timeLeft);
            C2D_Text t_vinst;
            C2D_TextParse(&t_vinst, g_dynBuf, inst);
            u32 instColor = game->iHaveVoted ? C2D_Color32(150, 150, 150, 255) : C2D_Color32(100, 255, 100, 255);
            C2D_DrawText(&t_vinst, C2D_WithColor | C2D_AlignCenter, 200, 90, 0.8f, 0.5f, 0.5f, instColor);
        }
        else if (game->showBanConfirm) {
            C2D_DrawRectSolid(20, 40, 0.8f, 360, 80, C2D_Color32(200, 50, 50, 200));
            C2D_DrawRectSolid(22, 42, 0.8f, 356, 76, C2D_Color32(40, 40, 45, 255));
            char title[64];
            snprintf(title, sizeof(title), "Kick %s ?", game->voteTargetName);
            C2D_Text t_vtitle;
            C2D_TextParse(&t_vtitle, g_dynBuf, title);
            C2D_DrawText(&t_vtitle, C2D_WithColor | C2D_AlignCenter, 200, 48, 0.8f, 0.6f, 0.6f, C2D_Color32(255, 100, 100, 255));
            C2D_Text t_vinfo;
            C2D_TextParse(&t_vinfo, g_dynBuf, "Requests Admin review to become global.");
            C2D_DrawText(&t_vinfo, C2D_WithColor | C2D_AlignCenter, 200, 70, 0.8f, 0.5f, 0.5f, C2D_Color32(200, 200, 200, 255));
            C2D_Text t_vinst;
            C2D_TextParse(&t_vinst, g_dynBuf, "[A] CONFIRM   [B] CANCEL");
            C2D_DrawText(&t_vinst, C2D_WithColor | C2D_AlignCenter, 200, 90, 0.8f, 0.5f, 0.5f, C2D_Color32(100, 255, 100, 255));
        }
        else {
            if (activeCount > 0 && game->appState == STATE_CHAT) {
                if (game->uiSelectedUserIdx >= activeCount) game->uiSelectedUserIdx = 0;
                ActiveUser* tu = getActiveUserByIndex(game->selectedCategoryIdx, game->selectedSubIdx, game->uiSelectedUserIdx);
                if (tu && strlen(tu->mac) > 0) {
                    C2D_DrawRectSolid(0, 26, 0.5f, 400, 16, C2D_Color32(60, 60, 70, 255));
                    char selStr[64];
                    snprintf(selStr, sizeof(selStr), "< %s >   [A] Vote Kick", tu->name);
                    C2D_Text t_sel;
                    C2D_TextParse(&t_sel, g_dynBuf, selStr);
                    C2D_DrawText(&t_sel, C2D_WithColor | C2D_AlignCenter, 200, 27, 0.5f, 0.45f, 0.45f, C2D_Color32(255, 200, 50, 255));
                }
            }
        }
    }

    if (game && game->connectionFailed) {
        C2D_DrawRectSolid(0, 0, 0.9f, 400, 240, C2D_Color32(20, 20, 25, 230));
        C2D_DrawRectSolid(0, 80, 0.9f, 400, 80, C2D_Color32(180, 40, 40, 255));
        C2D_DrawRectSolid(0, 80, 0.9f, 400, 3, C2D_Color32(255, 100, 100, 255));
        C2D_DrawRectSolid(0, 157, 0.9f, 400, 3, C2D_Color32(255, 100, 100, 255));
        C2D_Text t_err1;
        C2D_TextParse(&t_err1, g_dynBuf, "Server Maintenance / Offline");
        C2D_DrawText(&t_err1, C2D_WithColor | C2D_AlignCenter, 200, 95, 0.95f, 0.7f, 0.7f, C2D_Color32(255, 255, 255, 255));
        C2D_Text t_err2;
        C2D_TextParse(&t_err2, g_dynBuf, "For more info, please check our Discord.");
        C2D_DrawText(&t_err2, C2D_WithColor | C2D_AlignCenter, 200, 120, 0.95f, 0.5f, 0.5f, C2D_Color32(255, 220, 220, 255));
        C2D_Text t_err3;
        C2D_TextParse(&t_err3, g_dynBuf, "Press[SELECT] to retry connection");
        C2D_DrawText(&t_err3, C2D_WithColor | C2D_AlignCenter, 200, 140, 0.95f, 0.45f, 0.45f, C2D_Color32(255, 200, 200, 255));
    }
}

void renderBottom() {
    C2D_TargetClear(bottom, C2D_Color32(35, 35, 40, 255));
    C2D_SceneBegin(bottom);

    if (game->appState == STATE_SAVE_MENU || game->appState == STATE_LOAD_MENU) {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, C2D_Color32(30, 30, 35, 255));
        C2D_Text t_title;
        const char* t = (game->appState == STATE_SAVE_MENU) ? "Select Slot to SAVE" : "Select Slot to LOAD";
        C2D_TextParse(&t_title, g_dynBuf, t);
        C2D_DrawText(&t_title, C2D_WithColor | C2D_AlignCenter, 160, 10, 0.5f, 0.7f, 0.7f, C2D_Color32(255, 255, 255, 255));

        int slotW = 66, slotH = 46;
        int padX = 8, padY = 8;
        int startX = 16, startY = 35;

        for (int i=0; i<MAX_SAVE_SLOTS; i++) {
            int row = i / 4;
            int col = i % 4;
            int x = startX + col * (slotW + padX);
            int y = startY + row * (slotH + padY);

            C2D_DrawRectSolid(x, y, 0.5f, slotW, slotH, C2D_Color32(50, 50, 55, 255));
            C2D_DrawLine(x, y, C2D_Color32(100,100,100,255), x+slotW, y, C2D_Color32(100,100,100,255), 1.0f, 0.5f);
            C2D_DrawLine(x, y+slotH, C2D_Color32(100,100,100,255), x+slotW, y+slotH, C2D_Color32(100,100,100,255), 1.0f, 0.5f);
            C2D_DrawLine(x, y, C2D_Color32(100,100,100,255), x, y+slotH, C2D_Color32(100,100,100,255), 1.0f, 0.5f);
            C2D_DrawLine(x+slotW, y, C2D_Color32(100,100,100,255), x+slotW, y+slotH, C2D_Color32(100,100,100,255), 1.0f, 0.5f);

            if (game->slotInUse[i]) {
                renderSnapshotPreview(&game->savedDrawings[i], x+2, y+2, slotW-4, slotH-4);
            } else {
                C2D_Text t_empty;
                C2D_TextParse(&t_empty, g_dynBuf, "Empty");
                C2D_DrawText(&t_empty, C2D_WithColor | C2D_AlignCenter, x + slotW/2, y + slotH/2 - 6, 0.5f, 0.4f, 0.4f, C2D_Color32(150, 150, 150, 255));
            }
            
            char num[4]; snprintf(num, sizeof(num), "%d", i+1);
            C2D_Text t_num; C2D_TextParse(&t_num, g_dynBuf, num);
            C2D_DrawText(&t_num, C2D_WithColor, x+3, y+3, 0.5f, 0.4f, 0.4f, C2D_Color32(200,200,200,255));
        }

        C2D_DrawRectSolid(100, 210, 0.5f, 120, 20, C2D_Color32(150, 50, 50, 255));
        C2D_Text t_cancel; C2D_TextParse(&t_cancel, g_dynBuf, "Cancel");
        C2D_DrawText(&t_cancel, C2D_WithColor | C2D_AlignCenter, 160, 213, 0.5f, 0.5f, 0.5f, C2D_Color32(255,255,255,255));
        return;
    }

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
                Point p1 = game->userDrawing[i-1];
                Point p2 = game->userDrawing[i];
                if (p1.x == 0xFFFF || p2.x == 0xFFFF) continue;
                float displaySize = p2.thickness;
                u32 col = getPointColor(p2);
                C2D_DrawLine(p1.x, p1.y, col, p2.x, p2.y, col, displaySize, 0.5f);
                if (displaySize >= 1.5f) {
                    if (i == 1 || game->userDrawing[i-2].x == 0xFFFF) {
                        C2D_DrawCircleSolid(p1.x, p1.y, 0.5f, displaySize / 2.0f, col);
                    }
                    if (i == game->userDrawingCount - 1 || game->userDrawing[i+1].x == 0xFFFF) {
                        C2D_DrawCircleSolid(p2.x, p2.y, 0.5f, displaySize / 2.0f, col);
                    }
                }
            }
        }

        for (int i = 0; i < NUM_USER_COLORS; i++) {
            int x = COLOR_BTN_START_X + i * (COLOR_BTN_SIZE + COLOR_BTN_SPACING);
            u32 color = USER_COLORS[i];
            C2D_DrawRectSolid(x, COLOR_BAR_Y, 0.5f, COLOR_BTN_SIZE, COLOR_BTN_SIZE, color);
            if (game->currentColorIdx == i && !game->rainbowMode) {
                C2D_DrawRectSolid(x-1, COLOR_BAR_Y-1, 0.5f, COLOR_BTN_SIZE+2, 2, C2D_Color32(255,255,255,255));
                C2D_DrawRectSolid(x-1, COLOR_BAR_Y+COLOR_BTN_SIZE-1, 0.5f, COLOR_BTN_SIZE+2, 2, C2D_Color32(255,255,255,255));
                C2D_DrawRectSolid(x-1, COLOR_BAR_Y, 0.5f, 2, COLOR_BTN_SIZE, C2D_Color32(255,255,255,255));
                C2D_DrawRectSolid(x+COLOR_BTN_SIZE-1, COLOR_BAR_Y, 0.5f, 2, COLOR_BTN_SIZE, C2D_Color32(255,255,255,255));
            }
        }
        int x_rainbow = COLOR_BTN_START_X + NUM_USER_COLORS * (COLOR_BTN_SIZE + COLOR_BTN_SPACING);
        for (int k = 0; k < COLOR_BTN_SIZE; k++) {
            float pos = (float)k / COLOR_BTN_SIZE;
            u32 col = rainbowColor(pos);
            C2D_DrawRectSolid(x_rainbow + k, COLOR_BAR_Y, 0.5f, 1, COLOR_BTN_SIZE, col);
        }
        if (game->rainbowMode) {
            C2D_DrawRectSolid(x_rainbow-1, COLOR_BAR_Y-1, 0.5f, COLOR_BTN_SIZE+2, 2, C2D_Color32(255,255,255,255));
            C2D_DrawRectSolid(x_rainbow-1, COLOR_BAR_Y+COLOR_BTN_SIZE-1, 0.5f, COLOR_BTN_SIZE+2, 2, C2D_Color32(255,255,255,255));
            C2D_DrawRectSolid(x_rainbow-1, COLOR_BAR_Y, 0.5f, 2, COLOR_BTN_SIZE, C2D_Color32(255,255,255,255));
            C2D_DrawRectSolid(x_rainbow+COLOR_BTN_SIZE-1, COLOR_BAR_Y, 0.5f, 2, COLOR_BTN_SIZE, C2D_Color32(255,255,255,255));
        }

        C2D_DrawRectSolid(0, SECOND_BAR_Y, 0.5f, 320, 26, C2D_Color32(40, 40, 45, 255));
        
        // UNDO / REDO
        C2D_DrawRectSolid(0, SECOND_BAR_Y, 0.5f, 64, 26, C2D_Color32(50, 50, 55, 255));
        C2D_DrawLine(0, SECOND_BAR_Y + 13, C2D_Color32(80, 80, 85, 255), 64, SECOND_BAR_Y + 13, C2D_Color32(80, 80, 85, 255), 1.0f, 0.5f);
        C2D_DrawTriangle(32, SECOND_BAR_Y + 3, C2D_Color32(200, 200, 200, 255),
                         22, SECOND_BAR_Y + 10, C2D_Color32(200, 200, 200, 255),
                         42, SECOND_BAR_Y + 10, C2D_Color32(200, 200, 200, 255), 0.5f);
        C2D_DrawTriangle(22, SECOND_BAR_Y + 16, C2D_Color32(200, 200, 200, 255),
                         42, SECOND_BAR_Y + 16, C2D_Color32(200, 200, 200, 255),
                         32, SECOND_BAR_Y + 23, C2D_Color32(200, 200, 200, 255), 0.5f);
                         
        // SAVE / LOAD Buttons
        C2D_DrawRectSolid(224, SECOND_BAR_Y, 0.5f, 48, 26, C2D_Color32(150, 100, 50, 255));
        C2D_Text t_save; C2D_TextParse(&t_save, g_dynBuf, "SAVE");
        C2D_DrawText(&t_save, C2D_WithColor | C2D_AlignCenter, 248, SECOND_BAR_Y + 6, 0.5f, 0.45f, 0.45f, C2D_Color32(255,255,255,255));
        
        C2D_DrawRectSolid(272, SECOND_BAR_Y, 0.5f, 48, 26, C2D_Color32(50, 100, 150, 255));
        C2D_Text t_load; C2D_TextParse(&t_load, g_dynBuf, "LOAD");
        C2D_DrawText(&t_load, C2D_WithColor | C2D_AlignCenter, 296, SECOND_BAR_Y + 6, 0.5f, 0.45f, 0.45f, C2D_Color32(255,255,255,255));

        // SLIDER
        C2D_DrawRectSolid(64, SECOND_BAR_Y, 0.5f, 160, 26, C2D_Color32(45, 45, 50, 255));
        int sW = 120;
        int sX = 64 + (160 - sW) / 2;
        int sY = SECOND_BAR_Y + 10;
        C2D_DrawRectSolid(sX, sY, 0.5f, sW, 6, C2D_Color32(80, 80, 80, 255));
        float cSize = game->isEraser ? game->currentEraserSize : game->currentPenSize;
        float pct = (cSize - 1.0f) / 9.0f;
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 1.0f) pct = 1.0f;
        C2D_DrawRectSolid(sX, sY, 0.5f, sW * pct, 6, C2D_Color32(100, 180, 255, 255));
        C2D_DrawCircleSolid(sX + sW * pct, sY + 3, 0.5f, 8.0f, C2D_Color32(255, 255, 255, 255));

        const int btnY = TOOLBAR_Y_START, btnH = TOOLBAR_HEIGHT;
        C2D_DrawRectSolid(0, btnY, 0.5f, 64, btnH, C2D_Color32(180, 50, 50, 255));
        C2D_Text t_clear;
        C2D_TextParse(&t_clear, g_dynBuf, "CLR");
        C2D_DrawText(&t_clear, C2D_WithColor | C2D_AlignCenter, 32, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        u32 penCol = game->isEraser ? C2D_Color32(100, 100, 100, 255) : C2D_Color32(50, 120, 200, 255);
        C2D_DrawRectSolid(64, btnY, 0.5f, 64, btnH, penCol);
        C2D_Text t_pen;
        C2D_TextParse(&t_pen, g_dynBuf, "PEN");
        C2D_DrawText(&t_pen, C2D_WithColor | C2D_AlignCenter, 96, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        u32 eraserCol = game->isEraser ? C2D_Color32(50, 120, 200, 255) : C2D_Color32(100, 100, 100, 255);
        C2D_DrawRectSolid(128, btnY, 0.5f, 64, btnH, eraserCol);
        C2D_Text t_eraser;
        C2D_TextParse(&t_eraser, g_dynBuf, "ERS");
        C2D_DrawText(&t_eraser, C2D_WithColor | C2D_AlignCenter, 160, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        C2D_DrawRectSolid(192, btnY, 0.5f, 64, btnH, C2D_Color32(150, 50, 150, 255));
        C2D_Text t_text;
        C2D_TextParse(&t_text, g_dynBuf, "TXT");
        C2D_DrawText(&t_text, C2D_WithColor | C2D_AlignCenter, 224, btnY+7, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));
        C2D_DrawRectSolid(256, btnY, 0.5f, 64, btnH, C2D_Color32(50, 150, 50, 255));
        C2D_Text t_send;
        C2D_TextParse(&t_send, g_dynBuf, "SEND");
        C2D_DrawText(&t_send, C2D_WithColor | C2D_AlignCenter, 288, btnY+7, 0.5f, 0.55f, 0.55f, C2D_Color32(255, 255, 255, 255));

        if (!game->isEraser) {
            C2D_DrawLine(64, btnY, C2D_Color32(255,255,255,255), 128, btnY, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
            C2D_DrawLine(64, btnY+btnH, C2D_Color32(255,255,255,255), 128, btnY+btnH, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
            C2D_DrawLine(64, btnY, C2D_Color32(255,255,255,255), 64, btnY+btnH, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
            C2D_DrawLine(128, btnY, C2D_Color32(255,255,255,255), 128, btnY+btnH, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
        } else {
            C2D_DrawLine(128, btnY, C2D_Color32(255,255,255,255), 192, btnY, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
            C2D_DrawLine(128, btnY+btnH, C2D_Color32(255,255,255,255), 192, btnY+btnH, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
            C2D_DrawLine(128, btnY, C2D_Color32(255,255,255,255), 128, btnY+btnH, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
            C2D_DrawLine(192, btnY, C2D_Color32(255,255,255,255), 192, btnY+btnH, C2D_Color32(255,255,255,255), 2.0f, 0.5f);
        }
    } else {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, C2D_Color32(35, 35, 40, 255));
        C2D_Text t_title;
        C2D_TextParse(&t_title, g_dynBuf, "NoteRoom");
        C2D_DrawText(&t_title, C2D_WithColor | C2D_AlignCenter, 160, 12, 0.6f, 0.9f, 0.9f, C2D_Color32(255, 200, 80, 255));
        C2D_DrawLine(40, 45, C2D_Color32(100, 180, 255, 255), 280, 45, C2D_Color32(100, 180, 255, 255), 2.0f, 0.5f);
        char infoHeader[64];
        snprintf(infoHeader, sizeof(infoHeader), "Channel: %s", ROOM_NAMES[game->selectedCategoryIdx]);
        C2D_Text t_ch_name;
        C2D_TextParse(&t_ch_name, g_dynBuf, infoHeader);
        C2D_DrawText(&t_ch_name, C2D_WithColor | C2D_AlignCenter, 160, 60, 0.5f, 0.65f, 0.65f, C2D_Color32(255, 255, 255, 255));
        C2D_Text t_desc;
        C2D_TextParse(&t_desc, g_dynBuf, ROOM_DESCRIPTIONS[game->selectedCategoryIdx]);
        C2D_DrawText(&t_desc, C2D_WithColor | C2D_AlignCenter, 160, 85, 0.5f, 0.55f, 0.55f, C2D_Color32(150, 220, 150, 255));
        C2D_DrawLine(40, 120, C2D_Color32(100, 100, 100, 255), 280, 120, C2D_Color32(100, 100, 100, 255), 1.0f, 0.5f);
        C2D_Text t_rules_header;
        C2D_TextParse(&t_rules_header, g_dynBuf, "--- SERVER RULES ---");
        C2D_DrawText(&t_rules_header, C2D_WithColor | C2D_AlignCenter, 160, 135, 0.5f, 0.5f, 0.5f, C2D_Color32(200, 100, 100, 255));
        C2D_Text t_r1;
        C2D_TextParse(&t_r1, g_dynBuf, "1. Be respectful to all users.");
        C2D_DrawText(&t_r1, C2D_WithColor | C2D_AlignCenter, 160, 155, 0.5f, 0.5f, 0.5f, C2D_Color32(150, 150, 150, 255));
        C2D_Text t_r2;
        C2D_TextParse(&t_r2, g_dynBuf, "2. No NSFW content or drawings.");
        C2D_DrawText(&t_r2, C2D_WithColor | C2D_AlignCenter, 160, 170, 0.5f, 0.5f, 0.5f, C2D_Color32(150, 150, 150, 255));
        C2D_Text t_r3;
        C2D_TextParse(&t_r3, g_dynBuf, "3. Do not spam the chat.");
        C2D_DrawText(&t_r3, C2D_WithColor | C2D_AlignCenter, 160, 185, 0.5f, 0.5f, 0.5f, C2D_Color32(150, 150, 150, 255));
        C2D_Text t_r4;
        C2D_TextParse(&t_r4, g_dynBuf, "4. Do not share personal information.");
        C2D_DrawText(&t_r4, C2D_WithColor | C2D_AlignCenter, 160, 200, 0.5f, 0.5f, 0.5f, C2D_Color32(150, 150, 150, 255));
        C2D_Text t_version;
        C2D_TextParse(&t_version, g_dynBuf, "v1.0.4 Enjoy! discord.gg/PfsbvQTPV4");
        C2D_DrawText(&t_version, C2D_WithColor, 5, 220, 0.5f, 0.5f, 0.5f, C2D_Color32(80, 80, 90, 255));
    }
}