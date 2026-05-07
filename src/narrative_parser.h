#ifndef NARRATIVE_PARSER_H
#define NARRATIVE_PARSER_H

#include "config_parser.h" 
#include "irc_client.h"    

#define MAX_KEYWORD_LEN 100
#define MAX_NARRATIVES 100

typedef struct {
    char channel_context[MAX_CHANNEL_LEN];
    char keyword[MAX_KEYWORD_LEN];
    char response[IRC_MSG_BUFFER_SIZE];
} NarrativeEntry;

typedef struct {
    volatile int count; 
    volatile int version; 
    NarrativeEntry entries[MAX_NARRATIVES];
} SharedNarrativeBlock;

int load_narratives_into_shm(const char* narrative_dir, SharedNarrativeBlock* shm_block);

#endif
