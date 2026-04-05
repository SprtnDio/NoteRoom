#pragma once
#include <3ds.h>
#include "game.h"

void saveSnapshot(DrawingSnapshot* dest, Point* points, int pointCount, int* strokeStarts, int strokeCount);
void loadSnapshot(DrawingSnapshot* src, Point* points, int* pointCount, int* strokeStarts, int* strokeCount);
void decode_drawing(const char* payload);
void saveUndoState(void);
bool applyVertexPulling(Point* p1, Point* p2, float cx, float cy, float eraserRadius);
void compactDrawingArray(void);
void handleDrawingTouch(touchPosition touch, u32 currentTime);
void finishDrawingStroke(void);
void loadDrawingFromMessage(ChatMessage* msg);
void sendDrawing(void);