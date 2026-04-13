#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "drawing.h"
#include "ui.h"
#include "network.h"
#include "secrets.h"

void saveSnapshot(DrawingSnapshot* dest, Point* points, int pointCount, int* strokeStarts, int strokeCount) {
    dest->pointCount = pointCount;
    dest->strokeCount = strokeCount;
    if (pointCount > 0) {
        memcpy(dest->points, points, sizeof(Point) * pointCount);
        memcpy(dest->strokeStarts, strokeStarts, sizeof(int) * strokeCount);
    }
}

void loadSnapshot(DrawingSnapshot* src, Point* points, int* pointCount, int* strokeStarts, int* strokeCount) {
    *pointCount = src->pointCount;
    *strokeCount = src->strokeCount;
    if (src->pointCount > 0) {
        memcpy(points, src->points, sizeof(Point) * src->pointCount);
        memcpy(strokeStarts, src->strokeStarts, sizeof(int) * src->strokeCount);
    }
}

void decode_drawing(const char* payload) {
    static Point drawingPoints[MAX_INK_LIMIT];
    static int strokeStarts[MAX_STROKES];

    char senderName[16] = {0};
    int colorIdx = 0;
    char senderMac[MAC_BUFFER_SIZE] = {0};

    char* firstPipe = strchr(payload, '|');
    if (!firstPipe) return;

    char* hash1 = strchr(payload, '#');
    char* hash2 = hash1 ? strchr(hash1 + 1, '#') : NULL;

    if (hash1 && hash2 && hash2 < firstPipe) {
        int nameLen = hash1 - payload;
        if (nameLen > 15) nameLen = 15;
        snprintf(senderName, sizeof(senderName), "%.*s", nameLen, payload);
        colorIdx = atoi(hash1 + 1);
        int macLen = firstPipe - (hash2 + 1);
        snprintf(senderMac, sizeof(senderMac), "%.*s", macLen > MAX_MAC_DISPLAY ? MAX_MAC_DISPLAY : macLen, hash2 + 1);
    } else if (hash1 && hash1 < firstPipe) {
        int nameLen = hash1 - payload;
        if (nameLen > 15) nameLen = 15;
        snprintf(senderName, sizeof(senderName), "%.*s", nameLen, payload);
        colorIdx = atoi(hash1 + 1);
    } else {
        int nameLen = firstPipe - payload;
        if (nameLen > 15) nameLen = 15;
        snprintf(senderName, sizeof(senderName), "%.*s", nameLen, payload);
    }

    if (isBanned(senderMac)) return;
    if (colorIdx < 0 || colorIdx >= NUM_USER_COLORS) colorIdx = 0;
    
    if (strcmp(senderName, "ServerAdmin") == 0) {
        const char* content = firstPipe + 1;
        if (strncmp(content, "TEXT:", 5) == 0) {
            char* text = (char*)content + 5;
            u32 color = C2D_Color32(200, 50, 50, 255);
            bool is_banner = false;
            
            if (text[0] == '[' && text[1] == '#' && strlen(text) > 8 && text[8] == ']') {
                int r=0, g=0, b=0;
                if (sscanf(text, "[#%02x%02x%02x]", &r, &g, &b) == 3 || sscanf(text, "[#%02X%02X%02X]", &r, &g, &b) == 3) {
                    color = C2D_Color32(r, g, b, 255);
                    text += 9;
                }
            }

            if (strstr(text, " joined") || strstr(text, " left") || strstr(text, "Slowmode") || strstr(text, "Spam")) {
                is_banner = true;
            }

            if (is_banner) {
                updateStatus(text, color);
            } else {
                addMessage(senderName, 0, text, false, NULL, 0, NULL, 0, "");
            }
        }
        return;
    }

    if (strcmp(senderName, game->userName) == 0 && strcmp(senderName, "ServerAdmin") != 0) return;

    const char* content = firstPipe + 1;
    if (strncmp(content, "TEXT:", 5) == 0) {
        addMessage(senderName, colorIdx, content + 5, false, NULL, 0, NULL, 0, senderMac);
        return;
    }

    int pointIndex = 0;
    int strokeIndex = 0;
    int hexLen = strlen(content);

    for (int i = 0; i + 9 < hexLen && pointIndex < MAX_INK_LIMIT && strokeIndex < MAX_STROKES; i += 10) {
        char chunk[11] = {0};
        strncpy(chunk, content + i, 10);
        if (strcmp(chunk, "FFFFFFFF00") == 0) {
            if (pointIndex < MAX_INK_LIMIT) {
                drawingPoints[pointIndex++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
            }
            continue;
        }
        if (strlen(chunk) >= 10) {
            u8 x_low = (u8)strtol((char[]){chunk[0], chunk[1], '\0'}, NULL, 16);
            u8 x_high = (u8)strtol((char[]){chunk[2], chunk[3], '\0'}, NULL, 16);
            u8 y_low = (u8)strtol((char[]){chunk[4], chunk[5], '\0'}, NULL, 16);
            u8 y_high = (u8)strtol((char[]){chunk[6], chunk[7], '\0'}, NULL, 16);
            u8 prop = (u8)strtol((char[]){chunk[8], chunk[9], '\0'}, NULL, 16);
            u8 type = prop & 0x1;
            u8 sizeIdx = (prop >> 1) & 0x3;
            u8 color = (prop >> 3) & 0xF;
            u16 x = (u16)(x_low | (x_high << 8));
            u16 y = (u16)(y_low | (y_high << 8));
            
            if (x != 0xFFFF || y != 0xFFFF) {
                if (x > 319) x = 319;
                if (y < DRAWING_AREA_TOP) y = DRAWING_AREA_TOP;
                if (y > DRAWING_AREA_BOTTOM) y = DRAWING_AREA_BOTTOM;

                if (pointIndex == 0 || (pointIndex > 0 && drawingPoints[pointIndex-1].x == 0xFFFF)) {
                    if (strokeIndex < MAX_STROKES) { strokeStarts[strokeIndex++] = pointIndex; }
                }
                if (pointIndex < MAX_INK_LIMIT) {
                    float remoteThickness = (type == 1) ? game->eraserSizes[sizeIdx % ERASER_SIZE_COUNT] : game->penSizes[sizeIdx % PEN_SIZE_COUNT];
                    drawingPoints[pointIndex++] = (Point){
                        .x = x, .y = y, .type = type, .sizeIdx = sizeIdx,
                        .color = color, .thickness = remoteThickness
                    };
                }
            }
        }
    }
    if (pointIndex > 0) {
        addMessage(senderName, colorIdx, "", true, drawingPoints, pointIndex, strokeStarts, strokeIndex, senderMac);
    }
}

void saveUndoState() {
    if (game->undoCount >= MAX_UNDO_STEPS) {
        memmove(&game->undoSnapshots[0], &game->undoSnapshots[1], sizeof(DrawingSnapshot) * (MAX_UNDO_STEPS - 1));
        game->undoCount = MAX_UNDO_STEPS - 1;
    }
    saveSnapshot(&game->undoSnapshots[game->undoCount], game->userDrawing, game->userDrawingCount,
                 game->userStrokeStarts, game->userStrokeCount);
    game->undoCount++;
    game->redoCount = 0;
}

bool applyVertexPulling(Point* p1, Point* p2, float cx, float cy, float eraserRadius) {
    float ax = p1->x, ay = p1->y;
    float bx = p2->x, by = p2->y;
    float d1Sq = (ax - cx)*(ax - cx) + (ay - cy)*(ay - cy);
    float d2Sq = (bx - cx)*(bx - cx) + (by - cy)*(by - cy);
    float combinedRadius = eraserRadius + (p1->thickness / 2.0f);
    float rSq = combinedRadius * combinedRadius;
    float rSqBuffer = rSq + 1.0f;
    bool p1In = d1Sq <= rSqBuffer;
    bool p2In = d2Sq <= rSqBuffer;
    if (p1In && p2In) {
        if (p1->type < 252) p1->type = 255;
        if (p2->type < 252) p2->type = 255;
        return true;
    }
    float dx = bx - ax;
    float dy = by - ay;
    float a = dx*dx + dy*dy;
    if (fabs(a) < 0.0001f) return false;
    float fx = ax - cx;
    float fy = ay - cy;
    float b = 2.0f * (fx*dx + fy*dy);
    float c = (fx*fx + fy*fy) - rSq;
    float disc = b*b - 4.0f*a*c;
    if (disc >= 0) {
        disc = sqrtf(disc);
        float t1 = (-b - disc) / (2.0f * a);
        float t2 = (-b + disc) / (2.0f * a);
        if (!p1In && p2In) {
            if (t1 >= 0.0f && t1 <= 1.0f) {
                float nx = ax + t1 * dx;
                float ny = ay + t1 * dy;
                if (nx < 0.0f) nx = 0.0f; else if (nx > 400.0f) nx = 400.0f;
                if (ny < 0.0f) ny = 0.0f; else if (ny > 400.0f) ny = 400.0f;
                p2->x = (u16)nx;
                p2->y = (u16)ny;
                if (p2->type < 252) p2->type = 253;
            } else {
                if (p2->type < 252) p2->type = 255; 
            }
            return true;
        } else if (p1In && !p2In) {
            if (t2 >= 0.0f && t2 <= 1.0f) {
                float nx = ax + t2 * dx;
                float ny = ay + t2 * dy;
                if (nx < 0.0f) nx = 0.0f; else if (nx > 400.0f) nx = 400.0f;
                if (ny < 0.0f) ny = 0.0f; else if (ny > 400.0f) ny = 400.0f;
                p1->x = (u16)nx;
                p1->y = (u16)ny;
                if (p1->type < 252) p1->type = 252;
            } else {
                if (p1->type < 252) p1->type = 255;
            }
            return true;
        } else if (!p1In && !p2In) {
            if (t1 >= 0.0f && t1 <= 1.0f && t2 >= 0.0f && t2 <= 1.0f) {
                if (p2->type < 252) p2->type = 254;
                return true;
            }
        }
    }
    return false;
}

void compactDrawingArray() {
    static Point temp[MAX_INK_LIMIT * 3]; 
    int tempCount = 0;
    for (int i = 0; i < game->userDrawingCount; i++) {
        if (game->userDrawing[i].x == 0xFFFF || game->userDrawing[i].type == 255) {
            if (tempCount > 0 && temp[tempCount-1].x != 0xFFFF && tempCount < (MAX_INK_LIMIT * 3) - 1) {
                temp[tempCount++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
            }
        } else if (game->userDrawing[i].type == 254) {
            if (tempCount > 0 && temp[tempCount-1].x != 0xFFFF && tempCount < (MAX_INK_LIMIT * 3) - 2) {
                temp[tempCount++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
            }
            game->userDrawing[i].type = 0;
            if (tempCount < (MAX_INK_LIMIT * 3) - 1) {
                temp[tempCount++] = game->userDrawing[i];
            }
        } else if (game->userDrawing[i].type == 253) {
            game->userDrawing[i].type = 0;
            if (tempCount < (MAX_INK_LIMIT * 3) - 2) {
                temp[tempCount++] = game->userDrawing[i];
                if (tempCount > 0 && temp[tempCount-1].x != 0xFFFF) {
                    temp[tempCount++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
                }
            }
        } else if (game->userDrawing[i].type == 252) {
            if (tempCount > 0 && temp[tempCount-1].x != 0xFFFF && tempCount < (MAX_INK_LIMIT * 3) - 2) {
                temp[tempCount++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
            }
            game->userDrawing[i].type = 0;
            if (tempCount < (MAX_INK_LIMIT * 3) - 1) {
                temp[tempCount++] = game->userDrawing[i];
            }
        } else {
            if (tempCount < (MAX_INK_LIMIT * 3) - 1) {
                temp[tempCount++] = game->userDrawing[i];
            }
        }
    }
    static Point finalArr[MAX_INK_LIMIT];
    int finalCount = 0;
    for (int i = 0; i < tempCount; i++) {
        if (temp[i].x == 0xFFFF) {
            if (finalCount > 0 && finalArr[finalCount-1].x != 0xFFFF && finalCount < MAX_INK_LIMIT) {
                finalArr[finalCount++] = temp[i];
            }
        } else {
            bool prevIsSep = (i == 0 || temp[i-1].x == 0xFFFF);
            bool nextIsSep = (i == tempCount - 1 || temp[i+1].x == 0xFFFF);
            if (!prevIsSep || !nextIsSep) { 
                if (finalCount < MAX_INK_LIMIT) {
                    finalArr[finalCount++] = temp[i];
                }
            }
        }
    }
    while (finalCount > 0 && finalArr[finalCount-1].x == 0xFFFF) finalCount--;
    memcpy(game->userDrawing, finalArr, sizeof(Point) * finalCount);
    game->userDrawingCount = finalCount;
    game->userStrokeCount = 0;
    for (int i = 0; i < finalCount; i++) {
        if (i == 0 || (finalArr[i-1].x == 0xFFFF && finalArr[i].x != 0xFFFF)) {
            if (game->userStrokeCount < MAX_STROKES) {
                game->userStrokeStarts[game->userStrokeCount++] = i;
            }
        }
    }
}

void handleDrawingTouch(touchPosition touch, u32 currentTime) {
    if (touch.px > 319) touch.px = 319;
    if (touch.py < DRAWING_AREA_TOP) touch.py = DRAWING_AREA_TOP;
    if (touch.py > DRAWING_AREA_BOTTOM) touch.py = DRAWING_AREA_BOTTOM;
    
    if (game->isEraser) {
        if (!game->isDrawing) {
            saveUndoState();
            game->isDrawing = true;
            game->lastValidTouch = touch;
        }
        bool erasedSomething = false;
        float radius = game->currentEraserSize / 2.0f;
        int steps = 1;
        int dx = touch.px - game->lastValidTouch.px;
        int dy = touch.py - game->lastValidTouch.py;
        float dist = sqrtf((float)(dx*dx + dy*dy));
        if (dist > 0.0f) {
            steps = (int)(dist / (radius * 0.5f));
            if (steps < 1) steps = 1;
            if (steps > 20) steps = 20;
        }
        for (int s = 1; s <= steps; s++) {
            float t = (float)s / steps;
            float interpX = game->lastValidTouch.px + dx * t;
            float interpY = game->lastValidTouch.py + dy * t;
            bool stepChanged = false;
            for (int i = 1; i < game->userDrawingCount; i++) {
                if (game->userDrawing[i-1].x == 0xFFFF || game->userDrawing[i].x == 0xFFFF) continue;
                if (game->userDrawing[i-1].type == 255 && game->userDrawing[i].type == 255) continue;
                bool changed = applyVertexPulling(&game->userDrawing[i-1], &game->userDrawing[i], interpX, interpY, radius);
                if (changed) stepChanged = true;
            }
            if (stepChanged) {
                compactDrawingArray();
                erasedSomething = true;
            }
        }
        if (erasedSomething) {
            game->hasUnsavedDrawing = true;
            game->needsRedrawBottom = true;
        }
        game->lastValidTouch = touch;
        game->lastTouchTime = currentTime;
        return;
    }

    if (game->userDrawingCount >= MAX_INK_LIMIT - 2) {
        if (game->isDrawing) {
            game->userDrawing[game->userDrawingCount++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
            saveUndoState();
            game->isDrawing = false;
            game->needsRedrawBottom = true;
        }
        return;
    }
    
    u8 closestSizeIdx = (game->currentPenSize >= 6.0f) ? 2 : ((game->currentPenSize >= 3.0f) ? 1 : 0);

    if (!game->isDrawing) {
        saveUndoState();
        game->isDrawing = true;
        if (game->userDrawingCount > 0 && game->userDrawing[game->userDrawingCount-1].x != 0xFFFF) {
            if (game->userDrawingCount < MAX_INK_LIMIT - 2) {
                game->userDrawing[game->userDrawingCount++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
            }
        }
        if (game->userStrokeCount < MAX_STROKES &&
            (game->userDrawingCount == 0 || game->userDrawing[game->userDrawingCount-1].x == 0xFFFF)) {
            game->userStrokeStarts[game->userStrokeCount++] = game->userDrawingCount;
        }
        game->smoothX = touch.px;
        game->smoothY = touch.py;
        game->lastValidTouch = touch;
        u8 color = game->rainbowMode ? NUM_USER_COLORS : game->currentColorIdx;
        game->userDrawing[game->userDrawingCount++] = (Point){
            .x = touch.px, .y = touch.py,
            .type = 0,
            .sizeIdx = closestSizeIdx,
            .color = color,
            .thickness = game->currentPenSize
        };
        game->hasUnsavedDrawing = true;
        game->lastTouchTime = currentTime;
    } else {
        game->smoothX = game->smoothX * 0.4f + touch.px * 0.6f;
        game->smoothY = game->smoothY * 0.4f + touch.py * 0.6f;
        Point last = game->userDrawing[game->userDrawingCount-1];
        float dx = game->smoothX - last.x;
        float dy = game->smoothY - last.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist >= 2.0f && game->userDrawingCount < MAX_INK_LIMIT - 2) {
            u8 color = game->rainbowMode ? NUM_USER_COLORS : game->currentColorIdx;
            u8 closestSizeIdx = (game->currentPenSize >= 6.0f) ? 2 : ((game->currentPenSize >= 3.0f) ? 1 : 0);
            int steps = (int)(dist / 2.0f);
            if (steps < 1) steps = 1;
            for(int s = 1; s <= steps && game->userDrawingCount < MAX_INK_LIMIT - 2; s++) {
                float t = (float)s / steps;
                float fx = last.x + dx * t;
                float fy = last.y + dy * t;
                if (fx < 0.0f) fx = 0.0f; else if (fx > 319.0f) fx = 319.0f;
                if (fy < DRAWING_AREA_TOP) fy = DRAWING_AREA_TOP;
                if (fy > DRAWING_AREA_BOTTOM) fy = DRAWING_AREA_BOTTOM;
                u16 nx = (u16)fx;
                u16 ny = (u16)fy;
                game->userDrawing[game->userDrawingCount++] = (Point){
                    .x = nx, .y = ny,
                    .type = 0,
                    .sizeIdx = closestSizeIdx,
                    .color = color,
                    .thickness = game->currentPenSize 
                };
            }
            game->hasUnsavedDrawing = true;
            game->lastTouchTime = currentTime;
        }
        game->lastValidTouch = touch;
    }
    game->needsRedrawBottom = true;
}

void finishDrawingStroke() {
    if (game->isDrawing) {
        if (!game->isEraser && game->userDrawingCount > 0) {
            Point lastStored = game->userDrawing[game->userDrawingCount-1];
            if (lastStored.x != 0xFFFF && (lastStored.x != game->lastValidTouch.px || lastStored.y != game->lastValidTouch.py)) {
                if (game->userDrawingCount < MAX_INK_LIMIT - 2) {
                    u8 color = game->rainbowMode ? NUM_USER_COLORS : game->currentColorIdx;
                    u8 closestSizeIdx = (game->currentPenSize >= 6.0f) ? 2 : ((game->currentPenSize >= 3.0f) ? 1 : 0);
                    u16 tx = game->lastValidTouch.px;
                    u16 ty = game->lastValidTouch.py;
                    if (tx > 319) tx = 319;
                    if (ty < DRAWING_AREA_TOP) ty = DRAWING_AREA_TOP;
                    if (ty > DRAWING_AREA_BOTTOM) ty = DRAWING_AREA_BOTTOM;
                    game->userDrawing[game->userDrawingCount++] = (Point){
                        .x = tx, .y = ty,
                        .type = 0,
                        .sizeIdx = closestSizeIdx,
                        .color = color,
                        .thickness = game->currentPenSize
                    };
                }
            } else if (lastStored.x != 0xFFFF) {
                if (game->userDrawingCount < MAX_INK_LIMIT - 2) {
                    Point dup = lastStored;
                    dup.x += 1; 
                    game->userDrawing[game->userDrawingCount++] = dup;
                }
            }
            if (game->userDrawingCount < MAX_INK_LIMIT) {
                game->userDrawing[game->userDrawingCount++] = (Point){0xFFFF, 0xFFFF, 0, 0, 0, 0.0f};
            }
        }
        game->isDrawing = false;
        game->needsRedrawBottom = true;
    }
}

void loadDrawingFromMessage(ChatMessage* msg) {
    if (!msg || !msg->isDrawing || !msg->drawingData || msg->drawingCount == 0) return;
    if (msg->drawingCount > MAX_INK_LIMIT) {
        updateStatus("Drawing too large to load!", C2D_Color32(200, 50, 50, 255));
        return;
    }
    saveUndoState();
    for (int i = 0; i < msg->drawingCount && i < MAX_INK_LIMIT; i++) {
        game->userDrawing[i] = msg->drawingData[i];
    }
    game->userDrawingCount = msg->drawingCount;
    if (game->userDrawingCount > MAX_INK_LIMIT) game->userDrawingCount = MAX_INK_LIMIT;
    int copyCount = msg->strokeCount > MAX_STROKES ? MAX_STROKES : msg->strokeCount;
    if (copyCount > 0) {
        memcpy(game->userStrokeStarts, msg->strokeStarts, sizeof(int) * copyCount);
        game->userStrokeCount = copyCount;
    }
    game->needsRedrawBottom = true;
    game->needsRedrawTop = true;
    game->lastNetworkActivity = osGetTime();
}

static bool drawing_has_real_point() {
    for (int i = 0; i < game->userDrawingCount; i++) {
        if (game->userDrawing[i].x != 0xFFFF || game->userDrawing[i].y != 0xFFFF) {
            return true;
        }
    }
    return false;
}

void sendDrawing() {
    if (sendInProgress) return;
    if (game->isSyncing || isBanned(game->macAddress)) return;
    
    u64 now = osGetTime();
    if (now - game->lastSendTime < 5000) {
        updateStatus("Wait 5 seconds to send!", C2D_Color32(200, 150, 50, 255));
        return;
    }
    int requiredPoints = (MAX_INK_LIMIT * 1) / 100;
    if (game->userDrawingCount < requiredPoints) {
        updateStatus("Use at least 5% ink!", C2D_Color32(200, 150, 50, 255));
        return;
    }
    if (game->userDrawingCount <= 0) {
        updateStatus("No drawing to send!", C2D_Color32(200, 50, 50, 255));
        return;
    }
    if (!drawing_has_real_point()) {
        updateStatus("No points in drawing!", C2D_Color32(200, 50, 50, 255));
        game->userDrawingCount = 0; game->userStrokeCount = 0; game->needsRedrawBottom = true;
        return;
    }
    sendInProgress = true;
    updateStatus("Sending...", C2D_Color32(200, 150, 50, 255));
    int payloadSize = strlen(game->userName) + 50 + (game->userDrawingCount * 11) + 1;
    if (payloadSize > MAX_PAYLOAD_SIZE) {
        updateStatus("Too large!", C2D_Color32(200, 50, 50, 255));
        game->userDrawingCount = 0; game->userStrokeCount = 0; game->needsRedrawBottom = true;
        sendInProgress = false;
        return;
    }
    char* payload = (char*)malloc(payloadSize);
    if (!payload) { updateStatus("Memory Error!", C2D_Color32(200, 50, 50, 255)); sendInProgress = false; return; }
    int written = snprintf(payload, payloadSize, "%s#%d#%s|", game->userName, game->userColorIdx, game->macAddress);
    char* dest = payload + written;
    for(int i = 0; i < game->userDrawingCount; i++) {
        Point p = game->userDrawing[i];
        if(p.x == 0xFFFF) { 
            dest += snprintf(dest, payloadSize - (dest - payload), "FFFFFFFF00"); 
        } else {
            u8 prop = (p.color << 3) | (p.sizeIdx << 1) | (p.type & 0x1);
            dest += snprintf(dest, payloadSize - (dest - payload), "%02X%02X%02X%02X%02X",
                             p.x & 0xFF, (p.x >> 8) & 0xFF, p.y & 0xFF, (p.y >> 8) & 0xFF, prop);
        }
    }
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/Review/C%d/S%d", g_base_topic, game->selectedCategoryIdx, game->selectedSubIdx);
    mqtt_publish(topic, payload, false);
    free(payload);
    addMessage(game->userName, game->userColorIdx, "", true, game->userDrawing, game->userDrawingCount, game->userStrokeStarts, game->userStrokeCount, game->macAddress);
    game->userDrawingCount = 0; game->userStrokeCount = 0;
    game->undoCount = 0; game->redoCount = 0;
    game->hasUnsavedDrawing = false; game->needsRedrawBottom = true;
    game->lastSendTime = now;
    updateStatus("Sent!", C2D_Color32(50, 100, 255, 255));
    sendInProgress = false;
}

void saveDrawingsToSD(void) {
    ensureDirectoriesExist();
    FILE* f = fopen(save_file_path, "wb");
    if (f) {
        fwrite(game->slotInUse, sizeof(bool), MAX_SAVE_SLOTS, f);
        fwrite(game->savedDrawings, sizeof(DrawingSnapshot), MAX_SAVE_SLOTS, f);
        fclose(f);
    }
}

void loadDrawingsFromSD(void) {
    ensureDirectoriesExist();
    FILE* f = fopen(save_file_path, "rb");
    if (f) {
        fread(game->slotInUse, sizeof(bool), MAX_SAVE_SLOTS, f);
        fread(game->savedDrawings, sizeof(DrawingSnapshot), MAX_SAVE_SLOTS, f);
        fclose(f);
    } else {
        for(int i=0; i<MAX_SAVE_SLOTS; i++) game->slotInUse[i] = false;
    }
}