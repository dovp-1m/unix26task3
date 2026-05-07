#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "config_parser.h"

char *trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

int parse_config(const char *filename, BotConfig *config) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening config file");
        return -1;
    }

    memset(config, 0, sizeof(BotConfig));
    char line[MAX_CONFIG_LINE_LEN];
    config->num_channels = 0;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        char *key = strtok(line, "=");
        char *value = strtok(NULL, "");

        if (key && value) {
            key = trim_whitespace(key);
            value = trim_whitespace(value);

            if (strcmp(key, "server_ip") == 0) {
                strncpy(config->server_ip, value, sizeof(config->server_ip) - 1);
            } else if (strcmp(key, "server_port") == 0) {
                config->server_port = atoi(value);
            } else if (strcmp(key, "bot_nickname") == 0) {
                strncpy(config->bot_nickname, value, sizeof(config->bot_nickname) - 1);
            } else if (strcmp(key, "admin_user") == 0) {
                strncpy(config->admin_user, value, sizeof(config->admin_user) - 1);
            } else if (strcmp(key, "admin_pass") == 0) {
                strncpy(config->admin_pass, value, sizeof(config->admin_pass) - 1);
            } else if (strcmp(key, "admin_channel") == 0) {
                strncpy(config->admin_channel, value, sizeof(config->admin_channel) - 1);
            } else if (strcmp(key, "channels") == 0) {
                char *channel_token = strtok(value, ",");
                while (channel_token != NULL && config->num_channels < MAX_CHANNELS) {
                    char *trimmed_channel = trim_whitespace(channel_token);
                    if (strlen(trimmed_channel) > 0) {
                       strncpy(config->channels[config->num_channels], trimmed_channel, MAX_CHANNEL_LEN - 1);
                       config->num_channels++;
                    }
                    channel_token = strtok(NULL, ",");
                }
            }
        }
    }

    fclose(file);

    if (config->server_port == 0 || strlen(config->server_ip) == 0 || strlen(config->bot_nickname) == 0 || config->num_channels == 0) {
        fprintf(stderr, "Error: Missing critical configuration (server_ip, server_port, bot_nickname, or channels).\n");
        return -1;
    }

    char expected_nickname[MAX_NICK_LEN];
    snprintf(expected_nickname, sizeof(expected_nickname), "b%s", "dope1157");
    if (strcmp(config->bot_nickname, expected_nickname) != 0) {
         fprintf(stderr, "Warning: bot_nickname in config ('%s') does not match expected 'bdope1157'. Please check config/bot.conf.\n", config->bot_nickname);
    }
    return 0;
}
