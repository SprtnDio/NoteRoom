#pragma once
#include <3ds.h>

#define ENC_BAN_FILENAME "ban.dat"
#define BAN_FILENAME_LEN 7
#define ENC_TIME_FILENAME "time.dat"
#define TIME_FILENAME_LEN 8
#define ENC_USER_FILENAME "user.dat"
#define USER_FILENAME_LEN 8

#define ENC_ADMIN_TOKEN "admin"
#define ADMIN_TOKEN_LEN 5
#define ENC_BANLIST_TOPIC "banlist"
#define BANLIST_TOPIC_LEN 7

#define ENC_ADMIN_RESET_SUFFIX "reset"
#define ADMIN_RESET_SUFFIX_LEN 5
#define ENC_ADMIN_UNBAN_SUFFIX "unban"
#define ADMIN_UNBAN_SUFFIX_LEN 5
#define ENC_ADMIN_BAN_SUFFIX "ban"
#define ADMIN_BAN_SUFFIX_LEN 3
#define ENC_ADMIN_KICK_SUFFIX "kick"
#define ADMIN_KICK_SUFFIX_LEN 4
#define ENC_ADMIN_LIST_REQ_SUFFIX "listreq"
#define ADMIN_LIST_REQ_SUFFIX_LEN 7
#define ENC_ADMIN_STATUS_SUFFIX "status"
#define ADMIN_STATUS_SUFFIX_LEN 6
#define ENC_ADMIN_ANNOUNCE_SUFFIX "announce"
#define ADMIN_ANNOUNCE_SUFFIX_LEN 8

#define XOR_KEY_USER 0xAA
#define XOR_KEY_TIME 0xBB
#define XOR_KEY_BAN  0xCC

extern const char* g_mqtt_broker;
extern int g_mqtt_port;
extern const char* g_mqtt_user;
extern const char* g_mqtt_pass;
extern const char* g_base_topic;

void init_secrets(void);
void decrypt_string(char* dest, const char* src, int len, u8 key);