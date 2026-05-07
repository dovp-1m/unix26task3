#define _DEFAULT_SOURCE 
#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <signal.h> 
#include <errno.h>  
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h> 
#include <sys/stat.h> 
#include <sys/types.h> 
#include <time.h> 

#include "config_parser.h"
#include "irc_client.h"
#include "bot_logic.h"
#include "narrative_parser.h" 

#if defined(__linux__) || defined(__GNU_LIBRARY__)
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};
#endif

volatile sig_atomic_t running = 1;
volatile sig_atomic_t reload_narratives_flag = 0; 

#define APP_BUFFER_MAX_SIZE (IRC_MSG_BUFFER_SIZE * 4) 
char app_net_buffer[APP_BUFFER_MAX_SIZE];
int app_net_buffer_len = 0;
char main_irc_line_buffer[APP_BUFFER_MAX_SIZE]; // Renamed for clarity in registration
int main_irc_line_buffer_len = 0;


SharedNarrativeBlock *shared_narratives_ptr = NULL; 
int shmid_narratives = -1;
int semid_narratives = -1;

#define SHM_KEY_PATH "/tmp" 
#define SHM_KEY_ID 'N'
#define SEM_KEY_PATH "/tmp"
#define SEM_KEY_ID 'S'


void handle_sigint(int sig) {
    (void)sig; 
    fprintf(stdout, "\nBOT: Caught SIGINT, shutting down...\n");
    running = 0;
}

void handle_sigchld(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0); 
}

int attempt_bot_registration(IrcConnection *conn, const BotConfig *config, 
                             char *confirmed_nick_buf, size_t confirmed_nick_buf_size) {
    char current_nick_to_try[MAX_NICK_LEN];
    strncpy(current_nick_to_try, config->bot_nickname, MAX_NICK_LEN -1);
    current_nick_to_try[MAX_NICK_LEN-1] = '\0';

    int nick_alteration_step = 0; 
    const int MAX_REGISTRATION_ATTEMPTS = 5; 
    struct timespec retry_delay = {0, 250000000L}; 

    for (int attempt = 0; attempt < MAX_REGISTRATION_ATTEMPTS && running; ++attempt) {
        if (attempt > 0) { 
            nick_alteration_step++;
            strncpy(current_nick_to_try, config->bot_nickname, MAX_NICK_LEN -1); 
            current_nick_to_try[MAX_NICK_LEN-1] = '\0'; 

            if (strlen(config->bot_nickname) == 9) { // NICKLEN=9 specific
                switch (nick_alteration_step) {
                    case 1: current_nick_to_try[8] = '_'; break; 
                    case 2: current_nick_to_try[8] = 'z'; break; 
                    case 3: current_nick_to_try[7] = '_'; break; 
                    case 4: current_nick_to_try[0] = (config->bot_nickname[0] == 'b' ? 'a' : 'x'); break;
                    default:
                        fprintf(stdout, "BOT_REG: Exhausted NICK alteration strategies for NICKLEN=9.\n");
                        return 0; 
                }
            } else if (strlen(config->bot_nickname) < 9) {
                 snprintf(current_nick_to_try, MAX_NICK_LEN, "%.*s%d", 
                          8, config->bot_nickname, nick_alteration_step);
            } else { 
                 fprintf(stdout, "BOT_REG: Base nick '%s' is too long for NICKLEN=9.\n", config->bot_nickname);
                 return 0; 
            }
            current_nick_to_try[MAX_NICK_LEN-1] = '\0';

            fprintf(stdout, "BOT_REG: Previous NICK failed or taken, trying NICK: %s\n", current_nick_to_try);
        } else {
            fprintf(stdout, "BOT_REG: Attempting NICK: %s\n", current_nick_to_try);
            if (strlen(current_nick_to_try) > 9) { 
                fprintf(stdout, "BOT_REG: Configured nick '%s' is too long for NICKLEN=9.\n", current_nick_to_try);
                return 0;
            }
        }

        if (irc_send_raw(conn->sockfd, "NICK %s", current_nick_to_try) != 0) return 0; 
        if (irc_send_raw(conn->sockfd, "USER %s 0 * :%s", config->bot_nickname, config->bot_nickname) != 0) return 0;

        time_t registration_attempt_start_time = time(NULL);
        int got_registration_response_for_this_nick = 0;

        while (running && (time(NULL) - registration_attempt_start_time < 10)) { 
            int bytes_received = irc_receive_raw(conn); 
            if (bytes_received > 0) {
                if (main_irc_line_buffer_len + bytes_received < APP_BUFFER_MAX_SIZE) {
                    memcpy(main_irc_line_buffer + main_irc_line_buffer_len, conn->buffer, bytes_received);
                    main_irc_line_buffer_len += bytes_received;
                } else { main_irc_line_buffer_len = 0; }

                int line_start_idx = 0;
                for (int k = 0; k < main_irc_line_buffer_len; ++k) {
                    if (k + 1 < main_irc_line_buffer_len && main_irc_line_buffer[k] == '\r' && main_irc_line_buffer[k+1] == '\n') {
                        char line_buf[IRC_MSG_BUFFER_SIZE];
                        int single_line_len = k - line_start_idx;
                        if(single_line_len >= IRC_MSG_BUFFER_SIZE) single_line_len = IRC_MSG_BUFFER_SIZE -1;
                        memcpy(line_buf, main_irc_line_buffer + line_start_idx, single_line_len);
                        line_buf[single_line_len] = '\0';
                        
                        fprintf(stdout, "BOT_REG: RECV: %s\n", line_buf);

                        char actual_nick_from_001[MAX_NICK_LEN];
                        if (sscanf(line_buf, ":%*s 001 %49s :", actual_nick_from_001) == 1) {
                            strncpy(confirmed_nick_buf, actual_nick_from_001, confirmed_nick_buf_size -1);
                            confirmed_nick_buf[confirmed_nick_buf_size-1] = '\0';
                            fprintf(stdout, "BOT_REG: Registration successful! Actual NICK: %s\n", confirmed_nick_buf);
                            if (line_start_idx + single_line_len + 2 <= main_irc_line_buffer_len) {
                                memmove(main_irc_line_buffer, main_irc_line_buffer + line_start_idx + single_line_len + 2, main_irc_line_buffer_len - (line_start_idx + single_line_len + 2));
                                main_irc_line_buffer_len -= (line_start_idx + single_line_len + 2);
                            } else {main_irc_line_buffer_len = 0;}
                            return 1; 
                        }

                        char nick_in_433[MAX_NICK_LEN];
                        if (sscanf(line_buf, ":%*s 433 * %49s :", nick_in_433) == 1 && strcmp(nick_in_433, current_nick_to_try) == 0) {
                            fprintf(stdout, "BOT_REG: NICK %s is in use (433).\n", current_nick_to_try);
                            got_registration_response_for_this_nick = 1; 
                            nanosleep(&retry_delay, NULL); 
                            break; 
                        }
                        
                        char nick_in_error[MAX_NICK_LEN]; 
                        if (sscanf(line_buf, ":%*s 432 * %49s :", nick_in_error) == 1 || sscanf(line_buf, ":%*s 436 * %49s :", nick_in_error) == 1) {
                            if (strcmp(nick_in_error, current_nick_to_try) == 0) {
                                 fprintf(stderr, "BOT_REG: Fatal NICK error for %s: %s.\n", current_nick_to_try, line_buf);
                                got_registration_response_for_this_nick = 1;
                                nanosleep(&retry_delay, NULL);
                                break; 
                            }
                        }
                        line_start_idx = k + 2; k++; 
                    }
                }
                if (line_start_idx > 0 && line_start_idx <= main_irc_line_buffer_len) {
                    memmove(main_irc_line_buffer, main_irc_line_buffer + line_start_idx, main_irc_line_buffer_len - line_start_idx);
                    main_irc_line_buffer_len -= line_start_idx;
                } else if (line_start_idx > main_irc_line_buffer_len) { main_irc_line_buffer_len = 0; }
                if (got_registration_response_for_this_nick) break; 
            } else if (bytes_received == 0) { return 0; } 
            else { if (errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN) return 0; }
            if(!running) return 0; 
            usleep(100000); 
        } 
        if (!got_registration_response_for_this_nick && running) {
            fprintf(stdout, "BOT_REG: Timeout for NICK %s.\n", current_nick_to_try);
        }
    } 
    fprintf(stderr, "BOT_REG: Failed to register a NICK after %d attempts.\n", MAX_REGISTRATION_ATTEMPTS);
    return 0; 
}


int trigger_narrative_load(const char* narrative_dir) {
    if (shmid_narratives == -1 || semid_narratives == -1) {
        fprintf(stderr, "BOT: Shared memory or semaphore not initialized for narrative load.\n");
        return -1;
    }
    struct sembuf sem_op_wait = {0, -1, SEM_UNDO}; 
    struct sembuf sem_op_signal = {0, 1, SEM_UNDO}; 
    fprintf(stdout, "BOT: Attempting to lock semaphore for narrative reload...\n");
    if (semop(semid_narratives, &sem_op_wait, 1) == -1) {
        perror("BOT: semop wait failed before narrative load"); return -1;
    }
    fprintf(stdout, "BOT: Semaphore locked. Forking narrative loader.\n");
    pid_t loader_pid = fork();
    if (loader_pid < 0) {
        perror("BOT: fork failed for narrative loader");
        semop(semid_narratives, &sem_op_signal, 1); return -1;
    } else if (loader_pid == 0) { 
        SharedNarrativeBlock *child_shm_ptr = (SharedNarrativeBlock *)shmat(shmid_narratives, NULL, 0);
        if (child_shm_ptr == (void *)-1) { perror("LOADER_CHILD: shmat failed"); exit(EXIT_FAILURE); }
        fprintf(stdout, "LOADER_CHILD: Attached to SHM. Loading narratives from %s...\n", narrative_dir);
        load_narratives_into_shm(narrative_dir, child_shm_ptr);
        shmdt(child_shm_ptr);
        fprintf(stdout, "LOADER_CHILD: Detached from SHM. Exiting.\n");
        exit(EXIT_SUCCESS);
    } else { 
        fprintf(stdout, "BOT: Waiting for narrative loader child (PID %d) to complete...\n", loader_pid);
        waitpid(loader_pid, NULL, 0); 
        fprintf(stdout, "BOT: Narrative loader child finished.\n");
        if (semop(semid_narratives, &sem_op_signal, 1) == -1) { 
            perror("BOT: semop signal failed after narrative load");
        } else { fprintf(stdout, "BOT: Semaphore released.\n"); }
    }
    return 0;
}

int main() {
    BotConfig config;
    IrcConnection conn; 
    conn.sockfd = -1; 
    union semun sem_union_arg;

    signal(SIGINT, handle_sigint);
    signal(SIGCHLD, handle_sigchld);

    if (parse_config("config/bot.conf", &config) != 0) {
        fprintf(stderr, "BOT: Failed to parse configuration.\n"); return EXIT_FAILURE;
    }
    fprintf(stdout, "BOT: Configuration loaded for single bot instance: %s\n", config.bot_nickname);

    key_t shm_key = ftok(SHM_KEY_PATH, SHM_KEY_ID);
    if (shm_key == -1) { perror("BOT: ftok for shm failed"); exit(EXIT_FAILURE); }
    shmid_narratives = shmget(shm_key, sizeof(SharedNarrativeBlock), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid_narratives == -1) {
        if (errno == EEXIST) { 
            shmid_narratives = shmget(shm_key, sizeof(SharedNarrativeBlock), 0666);
            if (shmid_narratives == -1) { perror("BOT: shmget (get existing) failed"); exit(EXIT_FAILURE); }
             fprintf(stdout, "BOT: Attached to existing narrative SHM ID: %d\n", shmid_narratives);
        } else { perror("BOT: shmget (create new) failed"); exit(EXIT_FAILURE); }
    } else { fprintf(stdout, "BOT: Created narrative SHM ID: %d\n", shmid_narratives); }

    shared_narratives_ptr = (SharedNarrativeBlock *)shmat(shmid_narratives, NULL, 0);
    if (shared_narratives_ptr == (void *)-1) { perror("BOT: shmat failed"); exit(EXIT_FAILURE); }
    fprintf(stdout, "BOT: Attached to shared memory for narratives.\n");
    if (errno != EEXIST) { 
        shared_narratives_ptr->count = 0; 
        shared_narratives_ptr->version = 0;
    }

    key_t sem_key = ftok(SEM_KEY_PATH, SEM_KEY_ID);
    if (sem_key == -1) { perror("BOT: ftok for sem failed"); exit(EXIT_FAILURE); }
    semid_narratives = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semid_narratives == -1) {
        if (errno == EEXIST) {
            semid_narratives = semget(sem_key, 1, 0666);
            if (semid_narratives == -1) { perror("BOT: semget (get existing) failed"); exit(EXIT_FAILURE); }
            fprintf(stdout, "BOT: Attached to existing narrative Semaphore ID: %d\n", semid_narratives);
        } else { perror("BOT: semget (create new) failed"); exit(EXIT_FAILURE); }
    } else {
        fprintf(stdout, "BOT: Created narrative Semaphore ID: %d\n", semid_narratives);
        sem_union_arg.val = 1; 
        if (semctl(semid_narratives, 0, SETVAL, sem_union_arg) == -1) {
            perror("BOT: semctl SETVAL failed"); exit(EXIT_FAILURE);
        }
        fprintf(stdout, "BOT: Initialized semaphore to 1.\n");
    }
    
    char narrative_dir_config[256];
    snprintf(narrative_dir_config, sizeof(narrative_dir_config), "config/narratives"); 
    trigger_narrative_load(narrative_dir_config);

    conn.sockfd = irc_connect(config.server_ip, config.server_port);
    if (conn.sockfd < 0) { exit(EXIT_FAILURE); }

    char actual_bot_nick[MAX_NICK_LEN];
    if (!attempt_bot_registration(&conn, &config, actual_bot_nick, sizeof(actual_bot_nick))) {
        fprintf(stderr, "BOT: IRC Registration Failed. Exiting.\n");
        if (conn.sockfd >= 0) close(conn.sockfd);
        exit(EXIT_FAILURE);
    }
    fprintf(stdout, "BOT: Successfully registered with NICK %s.\n", actual_bot_nick);
    
    app_net_buffer_len = main_irc_line_buffer_len; // Transfer any leftover buffer from registration
    if(app_net_buffer_len > 0) memcpy(app_net_buffer, main_irc_line_buffer, app_net_buffer_len);


    sleep(1); 

    for (int i = 0; i < config.num_channels; ++i) { 
        if (irc_join_channel(conn.sockfd, config.channels[i]) != 0) {
            fprintf(stderr, "BOT: Failed to join channel %s.\n", config.channels[i]);
        } else { fprintf(stdout, "BOT: Joined channel %s.\n", config.channels[i]); }
        sleep(1);
    }
    int admin_chan_joined = 0; 
    for(int i=0; i < config.num_channels; ++i) {
        if(strlen(config.admin_channel)>0 && strcmp(config.channels[i], config.admin_channel) == 0) {
            admin_chan_joined = 1; break;
        }
    }
    if(strlen(config.admin_channel)>0 && !admin_chan_joined){
        if (irc_join_channel(conn.sockfd, config.admin_channel) != 0) {
            fprintf(stderr, "BOT: Failed to join admin channel %s.\n", config.admin_channel);
        } else { fprintf(stdout, "BOT: Joined admin channel %s.\n", config.admin_channel); }
    }

    fprintf(stdout, "BOT: Entering main loop for NICK %s. Press Ctrl+C to exit.\n", actual_bot_nick);
    while (running) {
        if (reload_narratives_flag) {
            fprintf(stdout, "BOT: Admin requested narrative reload.\n");
            trigger_narrative_load(narrative_dir_config);
            reload_narratives_flag = 0; 
        }

        int bytes_received = irc_receive_raw(&conn); 
        if (bytes_received > 0) { 
            if (app_net_buffer_len + bytes_received < APP_BUFFER_MAX_SIZE) {
                memcpy(app_net_buffer + app_net_buffer_len, conn.buffer, bytes_received);
                app_net_buffer_len += bytes_received;
            } else { fprintf(stderr, "BOT: Network buffer overflow!\n"); app_net_buffer_len = 0; }
            int line_start = 0;
            for (int i = 0; i < app_net_buffer_len; ++i) {
                if (i + 1 < app_net_buffer_len && app_net_buffer[i] == '\r' && app_net_buffer[i+1] == '\n') {
                    char current_line[IRC_MSG_BUFFER_SIZE]; 
                    int line_len = i - line_start;
                    if (line_len >= IRC_MSG_BUFFER_SIZE) line_len = IRC_MSG_BUFFER_SIZE - 1; 
                    memcpy(current_line, app_net_buffer + line_start, line_len);
                    current_line[line_len] = '\0';
                    
                    process_irc_message(conn.sockfd, current_line, actual_bot_nick, 
                                        shared_narratives_ptr, semid_narratives, &config);
                    
                    line_start = i + 2; i++; 
                }
            }
            if (line_start > 0 && line_start <= app_net_buffer_len) { 
                memmove(app_net_buffer, app_net_buffer + line_start, app_net_buffer_len - line_start);
                app_net_buffer_len -= line_start;
            } else if (line_start > app_net_buffer_len) { app_net_buffer_len = 0;}
        } else if (bytes_received == 0) { 
            fprintf(stdout, "BOT: Server disconnected. Exiting.\n"); running = 0; break; 
        } else { 
            if (errno == EINTR && !running) { break; }
            else if (errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN) {
                 perror("BOT: Error on recv"); running = 0; break;
            }
            if (running) usleep(10000); 
        }
        if (running) usleep(50000); 
    }

    fprintf(stdout, "BOT: Exiting program gracefully.\n");
    if (conn.sockfd >= 0) { 
      fprintf(stdout, "BOT: Closing socket %d.\n", conn.sockfd);
      if (irc_send_raw(conn.sockfd, "QUIT :Bot %s shutting down.", actual_bot_nick) == 0) {
          fprintf(stdout, "BOT: Sent QUIT message.\n"); usleep(100000);
      }
      close(conn.sockfd);
    }
    
    if (shared_narratives_ptr != (void*)-1 && shared_narratives_ptr != NULL) {
        shmdt(shared_narratives_ptr);
        fprintf(stdout, "BOT: Detached from shared memory.\n");
    }
    
    if (shmid_narratives != -1) {
        if (shmctl(shmid_narratives, IPC_RMID, NULL) == 0) {
            fprintf(stdout, "BOT: Shared memory segment %d marked for removal.\n", shmid_narratives);
        }
    }
    if (semid_narratives != -1) {
         if (semctl(semid_narratives, 0, IPC_RMID, sem_union_arg) == 0) {
            fprintf(stdout, "BOT: Semaphore set %d marked for removal.\n", semid_narratives);
        }
    }
    
    fprintf(stdout, "BOT: Cleanup complete. Bye!\n");
    return EXIT_SUCCESS;
}
