#pragma once
#include <3ds.h>
#include <time.h>
#include <stdbool.h>
#include "constants.h"

typedef enum {
    STATE_MAIN_MENU,
    STATE_SUB_MENU,
    STATE_CHAT
} AppState;

typedef struct {
    u16 x, y;
    u8 type;
    u8 sizeIdx;
    u8 color;
    float thickness;
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
    char name[16];
    char mac[MAC_BUFFER_SIZE];
    time_t lastSeen;
} ActiveUser;

typedef struct {
    Point points[MAX_INK_LIMIT];
    int pointCount;
    int strokeStarts[MAX_STROKES];
    int strokeCount;
} DrawingSnapshot;

typedef struct {
    char mac[MAC_BUFFER_SIZE];
    char name[16];
    time_t banEnd;
} BannedUser;

typedef struct {
    RoomChat rooms[CATEGORY_COUNT][MAX_SUB_ROOMS];
    time_t roomActivity[CATEGORY_COUNT][MAX_SUB_ROOMS];
    ActiveUser activeUsers[CATEGORY_COUNT][MAX_SUB_ROOMS][MAX_ACTIVE_USERS];
    Point userDrawing[MAX_INK_LIMIT];
    int userStrokeStarts[MAX_STROKES];
    int userDrawingCount;
    int userStrokeCount;

    DrawingSnapshot undoSnapshots[MAX_UNDO_STEPS];
    int undoCount;
    DrawingSnapshot redoSnapshots[MAX_UNDO_STEPS];
    int redoCount;

    char userName[16];
    u8 userColorIdx;
    char macAddress[MAC_BUFFER_SIZE];
    char clientID[32];
    bool isEraser;
    float penSizes[PEN_SIZE_COUNT];
    float eraserSizes[ERASER_SIZE_COUNT];

    float currentPenSize;
    float currentEraserSize;

    AppState appState;
    bool inChat;
    bool autoScrollEnabled;
    bool isSyncing;
    int selectedCategoryIdx;
    int selectedSubIdx;
    char statusMsg[64];

    u64 statusMsgTimer;
    bool needsRedrawTop;
    bool needsRedrawBottom;
    bool isDrawing;
    touchPosition lastValidTouch;

    float smoothX;
    float smoothY;

    bool hasUnsavedDrawing;
    u64 lastTouchTime;
    u64 lastTopUiUpdate;
    bool connectionFailed;

    int selectedDrawingIdx;

    u8 currentColorIdx;
    bool rainbowMode;

    u64 lastSendTime;
    u64 lastNetworkActivity;

    bool voteActive;
    char voteTargetMac[MAC_BUFFER_SIZE];
    char voteTargetName[16];
    char voteInitiatorMac[MAC_BUFFER_SIZE];
    char voteInitiatorName[16];
    u64 voteEndTime;
    int voteYes;
    int voteNo;
    char votedMacs[LOBBY_MAX_USERS][MAC_BUFFER_SIZE];
    int votedCount;
    bool iHaveVoted;
    int uiSelectedUserIdx;

    bool showBanConfirm;

    u64 trustedUnixTime;
    u64 trustedTick;
    bool trustedTimeValid;
    u64 lastNtpSyncTime;
    bool ntpSyncInProgress;
} GameState;

extern GameState* game;
extern BannedUser bannedUsers[100];
extern int bannedCount;
extern u64 lastBanChangeTime;
extern char ban_file_path[64];
extern char time_file_path[64];
extern char user_file_path[64];

void xor_buffer(u8* buf, size_t len, u8 key);
void ensureDirectoriesExist(void);
void getBanRemainingTime(char *buffer, size_t size, const char *mac);
void saveUserDataEncrypted(void);
void loadUserDataEncrypted(void);
void saveTrustedTime(void);
void loadTrustedTime(void);
u64 getMonotonicTick(void);
time_t getTrustedTime(void);
void cleanExpiredBans(void);
void saveBannedList(void);
void loadBannedList(void);
bool isBanned(const char* mac);
bool addBanEntry(const char* mac, const char* name, time_t endTime);
int getActiveUserCount(int category, int subRoom);
ActiveUser* getActiveUserByIndex(int cat, int sub, int idx);
void saveUserData(void);
void loadOrAskUsername(void);
void editUsername(void);
void addMessage(const char* sender, u8 colorIdx, const char* text, bool isDrawing, Point* drawingData, int drawingCount, int* strokeStarts, int strokeCount);