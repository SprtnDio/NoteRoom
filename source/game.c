#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "game.h"
#include "secrets.h"
#include "ui.h"
#include "network.h"

GameState* game = NULL;
BannedUser bannedUsers[100];
int bannedCount = 0;
u64 lastBanChangeTime = 0;
char ban_file_path[64];
char time_file_path[64];
char user_file_path[64];

void xor_buffer(u8* buf, size_t len, u8 key) {
    for (size_t i = 0; i < len; i++) buf[i] ^= key;
}

void ensureDirectoriesExist() {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/NoteRoom", 0777);
}

void getBanRemainingTime(char *buffer, size_t size, const char *mac) {
    if (!mac || !buffer || size < 6) {
        snprintf(buffer, size, "00:00");
        return;
    }
    time_t now = getTrustedTime();
    if (now == 0) {
        snprintf(buffer, size, "??:??");
        return;
    }
    for (int i = 0; i < bannedCount; i++) {
        if (strcmp(bannedUsers[i].mac, mac) == 0) {
            long long diff = bannedUsers[i].banEnd - now;
            if (diff <= 0) {
                snprintf(buffer, size, "00:00");
                return;
            }
            int hours = diff / 3600;
            int minutes = (diff % 3600) / 60;
            snprintf(buffer, size, "%02d:%02d", hours, minutes);
            return;
        }
    }
    snprintf(buffer, size, "00:00");
}

void saveUserDataEncrypted() {
    ensureDirectoriesExist();
    FILE* f = fopen(user_file_path, "wb");
    if (!f) return;
    char buffer[256];
    int len = snprintf(buffer, sizeof(buffer), "%s\n%d\n", game->userName, game->userColorIdx);
    xor_buffer((u8*)buffer, len, XOR_KEY_USER);
    fwrite(buffer, 1, len, f);
    fclose(f);
}

void loadUserDataEncrypted() {
    ensureDirectoriesExist();
    FILE* f = fopen(user_file_path, "rb");
    if (f) {
        char buffer[256];
        size_t bytes = fread(buffer, 1, sizeof(buffer)-1, f);
        if (bytes > 0) {
            xor_buffer((u8*)buffer, bytes, XOR_KEY_USER);
            buffer[bytes] = '\0';
            sscanf(buffer, "%12s\n%hhu", game->userName, &game->userColorIdx);
        }
        fclose(f);
    }
    if (strlen(game->userName) == 0) {
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
        saveUserDataEncrypted();
    }
}

void saveTrustedTime() {
    ensureDirectoriesExist();
    FILE* f = fopen(time_file_path, "wb");
    if (f) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%llu %llu\n", game->trustedUnixTime, game->trustedTick);
        xor_buffer((u8*)buf, len, XOR_KEY_TIME);
        fwrite(buf, 1, len, f);
        fclose(f);
    }
}

void loadTrustedTime() {
    ensureDirectoriesExist();
    FILE* f = fopen(time_file_path, "rb");
    if (f) {
        char buf[64];
        size_t bytes = fread(buf, 1, sizeof(buf)-1, f);
        if (bytes > 0) {
            xor_buffer((u8*)buf, bytes, XOR_KEY_TIME);
            buf[bytes] = '\0';
            sscanf(buf, "%llu %llu", &game->trustedUnixTime, &game->trustedTick);
            game->trustedTimeValid = (game->trustedUnixTime > 0);
        }
        fclose(f);
    } else {
        game->trustedTimeValid = false;
        game->trustedUnixTime = 0;
        game->trustedTick = 0;
    }
}

u64 getMonotonicTick() { return svcGetSystemTick(); }

time_t getTrustedTime() {
    if (!game->trustedTimeValid) return 0;
    u64 nowTick = getMonotonicTick();
    u64 tickDiff = nowTick - game->trustedTick;
    u64 secDiff = tickDiff / 268111856;
    return (time_t)(game->trustedUnixTime + secDiff);
}

void cleanExpiredBans() {
    time_t now = getTrustedTime();
    if (now == 0) return;
    bool changed = false;
    bool myBanExpired = false;
    for (int i = 0; i < bannedCount; ) {
        if (now >= bannedUsers[i].banEnd) {
            if (strcmp(bannedUsers[i].mac, game->macAddress) == 0) {
                myBanExpired = true;
            }
            for (int j = i; j < bannedCount - 1; j++) {
                bannedUsers[j] = bannedUsers[j+1];
            }
            bannedCount--;
            changed = true;
        } else {
            i++;
        }
    }
    if (changed) {
        lastBanChangeTime = (u64)now;
        saveBannedList();
        if (myBanExpired) {
            updateStatus("Your ban has expired!");
            game->needsRedrawTop = true;
        }
    }
}

void saveBannedList() {
    ensureDirectoriesExist();
    FILE* f = fopen(ban_file_path, "wb");
    if (!f) return;
    
    char tsLine[64];
    int tsLen = snprintf(tsLine, sizeof(tsLine), "%llu\n", lastBanChangeTime);
    xor_buffer((u8*)tsLine, tsLen, XOR_KEY_BAN);
    fwrite(tsLine, 1, tsLen, f);

    for (int i = 0; i < bannedCount; i++) {
        char line[128];
        int len = snprintf(line, sizeof(line), "%s %s %lld\n", bannedUsers[i].mac, bannedUsers[i].name, (long long)bannedUsers[i].banEnd);
        xor_buffer((u8*)line, len, XOR_KEY_BAN);
        fwrite(line, 1, len, f);
    }
    fclose(f);
}

void loadBannedList() {
    ensureDirectoriesExist();
    FILE* f = fopen(ban_file_path, "rb");
    bannedCount = 0;
    lastBanChangeTime = 0;
    
    if (f) {
        char buffer[2048];
        size_t bytes = fread(buffer, 1, sizeof(buffer)-1, f);
        if (bytes > 0) {
            xor_buffer((u8*)buffer, bytes, XOR_KEY_BAN);
            buffer[bytes] = '\0';
            char* line = buffer;
            char* next;
            
            next = strchr(line, '\n');
            if (next) {
                *next = '\0';
                if (strspn(line, "0123456789") == strlen(line)) {
                    sscanf(line, "%llu", &lastBanChangeTime);
                } else {
                    *next = '\n';
                }
                if (next) line = next + 1;
            }

            while (line && *line && bannedCount < 100) {
                next = strchr(line, '\n');
                if (next) *next = '\0';
                char mac[MAC_BUFFER_SIZE];
                char name[16];
                long long endTime;
                if (sscanf(line, "%19s %15s %lld", mac, name, &endTime) >= 3) {
                    snprintf(bannedUsers[bannedCount].mac, sizeof(bannedUsers[bannedCount].mac), "%s", mac);
                    snprintf(bannedUsers[bannedCount].name, sizeof(bannedUsers[bannedCount].name), "%s", name);
                    bannedUsers[bannedCount].banEnd = (time_t)endTime;
                    bannedCount++;
                }
                if (next) line = next + 1;
                else break;
            }
        }
        fclose(f);
    }
    if (game && game->trustedTimeValid) cleanExpiredBans();
}

bool isBanned(const char* mac) {
    if (!mac || strlen(mac) == 0) return false;
    time_t now = getTrustedTime();
    for (int i = 0; i < bannedCount; i++) {
        if (strcmp(bannedUsers[i].mac, mac) == 0) {
            if (now == 0) return true; 
            if (now < bannedUsers[i].banEnd) return true;
        }
    }
    return false;
}

bool addBanEntry(const char* mac, const char* name, time_t endTime) {
    if (!mac || strlen(mac) == 0) return false;
    
    for (int i = 0; i < bannedCount; i++) {
        if (strcmp(bannedUsers[i].mac, mac) == 0) {
            bannedUsers[i].banEnd = endTime;
            lastBanChangeTime = (u64)getTrustedTime();
            saveBannedList();
            return true;
        }
    }
    
    if (bannedCount < 100) {
        snprintf(bannedUsers[bannedCount].mac, sizeof(bannedUsers[bannedCount].mac), "%s", mac);
        snprintf(bannedUsers[bannedCount].name, sizeof(bannedUsers[bannedCount].name), "%s", (name && strlen(name)>0) ? name : "Unknown");
        bannedUsers[bannedCount].banEnd = endTime;
        bannedCount++;
        lastBanChangeTime = (u64)getTrustedTime();
        saveBannedList();
        return true;
    }
    return false;
}

int getActiveUserCount(int category, int subRoom) {
    int count = 0;
    time_t now = getTrustedTime();
    if (now == 0) return 0;
    for (int u = 0; u < MAX_ACTIVE_USERS; u++) {
        if (game->activeUsers[category][subRoom][u].lastSeen > 0 &&
            (now - game->activeUsers[category][subRoom][u].lastSeen) <= 60) {
            count++;
        }
    }
    return count;
}

ActiveUser* getActiveUserByIndex(int cat, int sub, int idx) {
    int current = 0;
    time_t now = getTrustedTime();
    if (now == 0) return NULL;
    for (int u = 0; u < MAX_ACTIVE_USERS; u++) {
        if (game->activeUsers[cat][sub][u].lastSeen > 0 &&
            (now - game->activeUsers[cat][sub][u].lastSeen) <= 60) {
            if (current == idx) return &game->activeUsers[cat][sub][u];
            current++;
        }
    }
    return NULL;
}

void saveUserData() { saveUserDataEncrypted(); }
void loadOrAskUsername() { loadUserDataEncrypted(); }

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
    snprintf(msg->sender, sizeof(msg->sender), "%s", sender);
    msg->nameColorIdx = colorIdx;
    snprintf(msg->text, sizeof(msg->text), "%s", text);
    msg->isDrawing = isDrawing;
    msg->timestamp = (u32)(getTrustedTime());

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

    if (isDrawing && drawingData && drawingCount > 0 && drawingCount <= MAX_INK_LIMIT) {
        msg->drawingData = (Point*)malloc(sizeof(Point) * drawingCount);
        if (msg->drawingData) {
            memcpy(msg->drawingData, drawingData, sizeof(Point) * drawingCount);
            msg->drawingCount = drawingCount;
            if (strokeStarts && strokeCount > 0) {
                int copyCount = strokeCount > MAX_STROKES ? MAX_STROKES : strokeCount;
                memcpy(msg->strokeStarts, strokeStarts, sizeof(int) * copyCount);
                msg->strokeCount = copyCount;
            }
        }
    }
    room->messageCount++;
    room->hasMessages = true;
    if (game->autoScrollEnabled) {
        room->autoScroll = true;
    }
    game->needsRedrawTop = true;
    game->needsRedrawBottom = true;
    game->lastNetworkActivity = osGetTime();
}