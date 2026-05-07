#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <errno.h>

#include "irc_client.h"

int irc_connect(const char *server_ip, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("ERROR invalid server IP address");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("ERROR connecting to server");
        close(sockfd);
        return -1;
    }
    printf("Connected to IRC server %s:%d (socket %d)\n", server_ip, port, sockfd);
    return sockfd;
}

int irc_send_raw(int sockfd, const char *message_format, ...) {
    char buffer[IRC_MSG_BUFFER_SIZE];
    va_list args;

    va_start(args, message_format);
    vsnprintf(buffer, IRC_MSG_BUFFER_SIZE - 2, message_format, args);
    va_end(args);

    strcat(buffer, "\r\n");

    if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
        perror("ERROR writing to socket");
        return -1;
    }
    return 0;
}

int irc_receive_raw(IrcConnection *connection) {
    memset(connection->buffer, 0, IRC_MSG_BUFFER_SIZE);
    int n = recv(connection->sockfd, connection->buffer, IRC_MSG_BUFFER_SIZE - 1, 0);

    if (n < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
             perror("ERROR reading from socket");
        }
        return -1;
    } else if (n == 0) {
        printf("Socket %d: Server closed connection.\n", connection->sockfd);
        return 0;
    }
    return n;
}

int irc_register(int sockfd, const char *nickname, const char *username, const char *realname) {
    if (irc_send_raw(sockfd, "NICK %s", nickname) != 0) return -1;
    if (irc_send_raw(sockfd, "USER %s 0 * :%s", username, realname) != 0) return -1;
    return 0;
}

int irc_join_channel(int sockfd, const char *channel) {
    if (irc_send_raw(sockfd, "JOIN %s", channel) != 0) return -1;
    return 0;
}

int irc_send_privmsg(int sockfd, const char *target, const char *message) {
    if (irc_send_raw(sockfd, "PRIVMSG %s :%s", target, message) != 0) return -1;
    return 0;
}

int irc_send_pong(int sockfd, const char *daemon_name) {
    if (irc_send_raw(sockfd, "PONG :%s", daemon_name) != 0) return -1;
    return 0;
}
