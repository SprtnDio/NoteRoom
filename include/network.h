#pragma once
#include <3ds.h>
#include <stdbool.h>

extern int mqtt_sock;
extern u64 last_ping_time;
extern u64 last_reconnect_time;
extern u64 lastBanSyncRequest;
extern bool sendInProgress;
extern bool banRequestSent;

extern char admin_token[32];
extern char admin_reset_topic[128];
extern char admin_unban_topic[128];
extern char admin_ban_topic[128];
extern char admin_kick_topic[128];
extern char admin_list_req_topic[128];
extern char admin_list_res_topic[128];
extern char admin_status_topic[128];
extern char admin_announce_topic[128];
extern char admin_banlist_topic[128];

int mqtt_encode_length(u8* buf, int length);
void mqtt_connect(void);
void mqtt_subscribe(const char* topic);
void mqtt_unsubscribe(const char* topic);
void mqtt_publish(const char* topic, const char* payload, bool retain);
void mqtt_ping(void);
void mqtt_poll(void);
bool ntp_sync_time(void);
void handle_admin_command(const char* topic, const char* payload);