#pragma once
#include <citro2d.h>
#include "game.h"

extern C3D_RenderTarget *top, *bottom;
extern C2D_TextBuf g_dynBuf;

u32 rainbowColor(float pos);
u32 getPointColor(Point p);
void updateStatus(const char* t, u32 color);
void drawTextBubble(float x, float y, float width, float height, ChatMessage* msg);
void drawDrawingBubble(float x, float y, float width, float height, ChatMessage* msg);
void renderDrawingPreview(ChatMessage* msg, float startX, float startY, float width, float height);
void renderSnapshotPreview(DrawingSnapshot* snap, float startX, float startY, float width, float height);
void renderScrollBar(float totalHeight, float visibleHeight, float scrollOffset);
void drawActivityDot(float x, float y, int userCount);
void drawDrawingToolbar(void);
void sendTextMessage(const char* text);
void openKeyboard(void);
void renderTop(void);
void renderBottom(void);