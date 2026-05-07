#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#define MAX_CONFIG_LINE_LEN 256
#define MAX_CHANNELS 10
#define MAX_CHANNEL_LEN 50
#define MAX_NICK_LEN 50
#define MAX_USER_LEN 50
#define MAX_PASS_LEN 50

typedef struct {
    char server_ip[MAX_CONFIG_LINE_LEN];
    int server_port;
    char bot_nickname[MAX_NICK_LEN];
    char channels[MAX_CHANNELS][MAX_CHANNEL_LEN];
    int num_channels;
    char admin_user[MAX_USER_LEN];
    char admin_pass[MAX_PASS_LEN];
    char admin_channel[MAX_CHANNEL_LEN];
} BotConfig;

int parse_config(const char *filename, BotConfig *config);

#endif
