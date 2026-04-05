#include <string.h>
#include "secrets.h"

const char* g_mqtt_broker = "127.0.0.1"; // Default/Dummy, set real inside init_secrets() if needed
int g_mqtt_port = 1883;
const char* g_mqtt_user = "user";
const char* g_mqtt_pass = "pass";
const char* g_base_topic = "noteroom";

void init_secrets(void) {
    // Populate variables securely if needed
}

void decrypt_string(char* dest, const char* src, int len, u8 key) {
    for (int i = 0; i < len; i++) {
        dest[i] = src[i] ^ key;
    }
    dest[len] = '\0';
}