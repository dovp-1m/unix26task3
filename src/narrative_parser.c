#define _DEFAULT_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h> 
#include <sys/stat.h> 
#include <ctype.h> 

#include "narrative_parser.h"

#define MAX_NARRATIVE_FILE_LINE_LEN (MAX_KEYWORD_LEN + IRC_MSG_BUFFER_SIZE + 20)

static char *trim_narrative_line(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

int load_narratives_into_shm(const char* narrative_dir_path, SharedNarrativeBlock* shm_block) {
    if (!shm_block || !narrative_dir_path) return -1;

    DIR *d;
    struct dirent *dir;
    int narratives_loaded_count = 0;
    
    shm_block->version++; 

    d = opendir(narrative_dir_path);
    if (d) {
        while ((dir = readdir(d)) != NULL && narratives_loaded_count < MAX_NARRATIVES) {
            if (dir->d_type == DT_REG) { 
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", narrative_dir_path, dir->d_name);
                
                FILE *file = fopen(filepath, "r");
                if (!file) {
                    perror("Failed to open narrative file");
                    continue;
                }
                fprintf(stdout, "LOADER: Parsing narrative file: %s\n", filepath);

                char line[MAX_NARRATIVE_FILE_LINE_LEN];
                char current_channel_context[MAX_CHANNEL_LEN] = "*"; 
                char current_keyword[MAX_KEYWORD_LEN] = "";
                char current_response_buffer[IRC_MSG_BUFFER_SIZE * 5] = ""; 
                int reading_response = 0;

                while (fgets(line, sizeof(line), file)) {
                    char* trimmed_line = trim_narrative_line(line);

                    if (strncmp(trimmed_line, "CHANNEL=", 8) == 0) {
                        strncpy(current_channel_context, trimmed_line + 8, MAX_CHANNEL_LEN -1);
                        current_channel_context[MAX_CHANNEL_LEN-1] = '\0';
                        reading_response = 0;
                    } else if (strncmp(trimmed_line, "KEYWORD:", 8) == 0) {
                        if (reading_response && strlen(current_keyword) > 0 && narratives_loaded_count < MAX_NARRATIVES) {
                             strncpy(shm_block->entries[narratives_loaded_count].channel_context, current_channel_context, MAX_CHANNEL_LEN-1);
                             strncpy(shm_block->entries[narratives_loaded_count].keyword, current_keyword, MAX_KEYWORD_LEN-1);
                             strncpy(shm_block->entries[narratives_loaded_count].response, trim_narrative_line(current_response_buffer), IRC_MSG_BUFFER_SIZE-1);
                             shm_block->entries[narratives_loaded_count].channel_context[MAX_CHANNEL_LEN-1] = '\0';
                             shm_block->entries[narratives_loaded_count].keyword[MAX_KEYWORD_LEN-1] = '\0';
                             shm_block->entries[narratives_loaded_count].response[IRC_MSG_BUFFER_SIZE-1] = '\0';
                             narratives_loaded_count++;
                             current_response_buffer[0] = '\0';
                        }
                        strncpy(current_keyword, trimmed_line + 8, MAX_KEYWORD_LEN -1);
                        current_keyword[MAX_KEYWORD_LEN-1] = '\0';
                        reading_response = 0; 
                    } else if (strncmp(trimmed_line, "RESPONSE:", 9) == 0) {
                        current_response_buffer[0] = '\0'; 
                        strncat(current_response_buffer, trimmed_line + 9, sizeof(current_response_buffer) - strlen(current_response_buffer) -1);
                        reading_response = 1;
                    } else if (strcmp(trimmed_line, "---") == 0) {
                         if (strlen(current_keyword) > 0 && strlen(current_response_buffer) > 0 && narratives_loaded_count < MAX_NARRATIVES) {
                             strncpy(shm_block->entries[narratives_loaded_count].channel_context, current_channel_context, MAX_CHANNEL_LEN-1);
                             strncpy(shm_block->entries[narratives_loaded_count].keyword, current_keyword, MAX_KEYWORD_LEN-1);
                             strncpy(shm_block->entries[narratives_loaded_count].response, trim_narrative_line(current_response_buffer), IRC_MSG_BUFFER_SIZE-1);
                             shm_block->entries[narratives_loaded_count].channel_context[MAX_CHANNEL_LEN-1] = '\0';
                             shm_block->entries[narratives_loaded_count].keyword[MAX_KEYWORD_LEN-1] = '\0';
                             shm_block->entries[narratives_loaded_count].response[IRC_MSG_BUFFER_SIZE-1] = '\0';
                             narratives_loaded_count++;
                         }
                         current_keyword[0] = '\0';
                         current_response_buffer[0] = '\0';
                         reading_response = 0;
                    } else if (reading_response) {
                        if (strlen(current_response_buffer) > 0) strncat(current_response_buffer, " ", sizeof(current_response_buffer) - strlen(current_response_buffer) -1);
                        strncat(current_response_buffer, trimmed_line, sizeof(current_response_buffer) - strlen(current_response_buffer) -1);
                    }
                }
                if (strlen(current_keyword) > 0 && strlen(current_response_buffer) > 0 && narratives_loaded_count < MAX_NARRATIVES) {
                     strncpy(shm_block->entries[narratives_loaded_count].channel_context, current_channel_context, MAX_CHANNEL_LEN-1);
                     strncpy(shm_block->entries[narratives_loaded_count].keyword, current_keyword, MAX_KEYWORD_LEN-1);
                     strncpy(shm_block->entries[narratives_loaded_count].response, trim_narrative_line(current_response_buffer), IRC_MSG_BUFFER_SIZE-1);
                     shm_block->entries[narratives_loaded_count].channel_context[MAX_CHANNEL_LEN-1] = '\0';
                     shm_block->entries[narratives_loaded_count].keyword[MAX_KEYWORD_LEN-1] = '\0';
                     shm_block->entries[narratives_loaded_count].response[IRC_MSG_BUFFER_SIZE-1] = '\0';
                     narratives_loaded_count++;
                }
                fclose(file);
            }
        }
        closedir(d);
    } else {
        perror("Couldn't open narrative directory");
        return -1;
    }
    shm_block->count = narratives_loaded_count;
    fprintf(stdout, "LOADER: Loaded %d narratives into shared memory.\n", narratives_loaded_count);
    return narratives_loaded_count;
}
