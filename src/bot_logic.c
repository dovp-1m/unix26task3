#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <string.h>
#include <stdlib.h> 
#include <time.h>   
#include <ctype.h>  
#include <sys/sem.h> 
#include <sys/types.h> 
#include <signal.h>    
#include <unistd.h>    

#include "bot_logic.h"
#include "irc_client.h"    
#include "config_parser.h" 
#include "narrative_parser.h"
#include "ollama_client.h"

#if defined(__linux__) || defined(__GNU_LIBRARY__)
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};
#endif

void process_irc_message(int sockfd, const char *raw_line, 
                         const char *actual_bot_nick, 
                         SharedNarrativeBlock *narratives_shm,
                         int narratives_sem_id,
                         const BotConfig *config) {

    if (strncmp(raw_line, "PING :", 6) == 0) {
        const char *ping_param = raw_line + 6; 
        fprintf(stdout, "BOT_LOGIC (NICK: %s): Responding to PING with param: %s\n", actual_bot_nick, ping_param);
        irc_send_pong(sockfd, ping_param);
        return;
    }

    char prefix[256] = "";      
    char sender_nick[MAX_NICK_LEN] = "";
    char command[50] = "";
    char target[MAX_CHANNEL_LEN] = ""; 
    char message_text[IRC_MSG_BUFFER_SIZE] = ""; 

    if (raw_line[0] == ':') { 
        int items_scanned = sscanf(raw_line, ":%255s %49s %49s :%[^\r\n]", prefix, command, target, message_text);
        if (items_scanned < 3) { 
             items_scanned = sscanf(raw_line, ":%255s %49s :%[^\r\n]", prefix, command, message_text); 
             if (items_scanned >= 2) { strncpy(target, actual_bot_nick, sizeof(target)-1); target[sizeof(target)-1] = '\0';}
        }
         if (items_scanned >=1 && strchr(prefix, '!') != NULL) { sscanf(prefix, "%[^!]", sender_nick); }
         else if (items_scanned >= 1) { strncpy(sender_nick, prefix, sizeof(sender_nick)-1); sender_nick[sizeof(sender_nick)-1] = '\0';}
    } else { 
        if (sscanf(raw_line, "PING %s", message_text) == 1) {
            fprintf(stdout, "BOT_LOGIC (NICK: %s): Responding to PING (no prefix) with param: %s\n", actual_bot_nick, message_text);
            irc_send_pong(sockfd, message_text); return;
        }
    }

    if (strcmp(command, "PRIVMSG") == 0) {
        fprintf(stdout, "BOT_LOGIC (NICK: %s): PRIVMSG Target=[%s], Sender=[%s], Msg=[%s]\n", actual_bot_nick, target, sender_nick, message_text);

        if (strcmp(sender_nick, actual_bot_nick) == 0) return;
        if (sender_nick[0] == 'b' && strlen(sender_nick) > 5) { 
            int looks_like_custom_bot_id = 1;
            for (size_t i = 1; i < 5 && sender_nick[i] != '\0'; ++i) if (!isalpha(sender_nick[i])) {looks_like_custom_bot_id = 0; break;}
            if(looks_like_custom_bot_id) for (size_t i = 5; i < 9 && sender_nick[i] != '\0'; ++i) if (!isdigit(sender_nick[i])) {looks_like_custom_bot_id = 0; break;}
            if (looks_like_custom_bot_id && strlen(sender_nick) >= 9) return;
        }

        const char *reply_to_where = NULL;
        if (target[0] == '#') { reply_to_where = target; } 
        else if (strcmp(target, actual_bot_nick) == 0) { reply_to_where = sender_nick; }

        if (reply_to_where) {
            if (strcmp(target, config->admin_channel) == 0 && strcmp(sender_nick, config->admin_user) == 0) {
                 if (strncmp(message_text, "!say ", 5) == 0) {
                    char temp_message_text[IRC_MSG_BUFFER_SIZE]; 
                    strncpy(temp_message_text, message_text, IRC_MSG_BUFFER_SIZE-1);
                    temp_message_text[IRC_MSG_BUFFER_SIZE-1] = '\0';

                    char *say_target_channel = strtok(temp_message_text + 5, " ");
                    char *say_message = strtok(NULL, "\r\n"); // Consume until end of line
                    if (say_target_channel && say_message) {
                        irc_send_privmsg(sockfd, say_target_channel, say_message);
                        irc_send_privmsg(sockfd, reply_to_where, "Admin: Message sent.");
                    } else {
                        irc_send_privmsg(sockfd, reply_to_where, "Admin Usage: !say <#channel/user> <message>");
                    }
                    return; 
                } else if (strcmp(message_text, "!reload_narratives") == 0) {
                    irc_send_privmsg(sockfd, reply_to_where, "Admin: Requesting narrative reload...");
                    reload_narratives_flag = 1; 
                    return; 
                } else if (strcmp(message_text, "!shutdown") == 0) {
                    irc_send_privmsg(sockfd, reply_to_where, "Admin: Requesting bot shutdown...");
                    kill(getpid(), SIGINT); 
                    return;
                }
            }
            
            struct sembuf sem_op_wait = {0, -1, SEM_UNDO};
            struct sembuf sem_op_signal = {0, 1, SEM_UNDO};
            int narrative_found = 0;

            if (narratives_shm != NULL && narratives_sem_id != -1) {
                if (semop(narratives_sem_id, &sem_op_wait, 1) == -1) {
                    perror("BOT_LOGIC: semop wait for narratives failed");
                } else {
                    int current_narrative_count = narratives_shm->count; 
                    for (int i = 0; i < current_narrative_count; ++i) {
                        if ((strcmp(narratives_shm->entries[i].channel_context, "*") == 0 || 
                             strcmp(narratives_shm->entries[i].channel_context, target) == 0)) {
                            if (strlen(narratives_shm->entries[i].keyword)>0 && strstr(message_text, narratives_shm->entries[i].keyword) != NULL) {
                                irc_send_privmsg(sockfd, reply_to_where, narratives_shm->entries[i].response);
                                narrative_found = 1;
                                break; 
                            }
                        }
                    }
                    if (semop(narratives_sem_id, &sem_op_signal, 1) == -1) {
                        perror("BOT_LOGIC: semop signal for narratives failed");
                    }
                }
            }
            if (narrative_found) return; 

            if (strstr(message_text, "hello") != NULL || strstr(message_text, actual_bot_nick) != NULL) {
                char response_buffer[100];
                snprintf(response_buffer, sizeof(response_buffer), "Greetings, %s!", sender_nick);
                irc_send_privmsg(sockfd, reply_to_where, response_buffer);
            } else if (strcmp(message_text, "!time") == 0) {
                time_t now = time(NULL);
                char time_str_buffer[100];
                strftime(time_str_buffer, sizeof(time_str_buffer), "%c", localtime(&now)); 
                irc_send_privmsg(sockfd, reply_to_where, time_str_buffer);
            } else if (strcmp(message_text, "!ping") == 0) { 
                irc_send_privmsg(sockfd, reply_to_where, "pong!");
            }
        }
    } else if (strcmp(command, "001") == 0) { 
        fprintf(stdout, "BOT_LOGIC (NICK: %s): Received welcome from server (%s).\n", actual_bot_nick, sender_nick); 
    }
}
