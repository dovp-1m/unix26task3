#ifndef BOT_LOGIC_H
#define BOT_LOGIC_H

#include <signal.h> 

#include "irc_client.h"
#include "config_parser.h" 
#include "narrative_parser.h" 

extern volatile sig_atomic_t reload_narratives_flag; 

void process_irc_message(int sockfd, const char *raw_line, 
                         const char *actual_bot_nick, 
                         SharedNarrativeBlock *narratives_shm,
                         int narratives_sem_id,
                         const BotConfig *config);

#endif
