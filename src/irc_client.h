#ifndef IRC_CLIENT_H
#define IRC_CLIENT_H

#include <sys/socket.h>

#define IRC_MSG_BUFFER_SIZE 512

typedef struct {
    int sockfd;
    char buffer[IRC_MSG_BUFFER_SIZE];
} IrcConnection;

int irc_connect(const char *server_ip, int port);
int irc_send_raw(int sockfd, const char *message_format, ...);
int irc_receive_raw(IrcConnection *connection);
int irc_register(int sockfd, const char *nickname, const char *username, const char *realname);
int irc_join_channel(int sockfd, const char *channel);
int irc_send_privmsg(int sockfd, const char *target, const char *message);
int irc_send_pong(int sockfd, const char *daemon_name);

#endif
