#include "constants.h"

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