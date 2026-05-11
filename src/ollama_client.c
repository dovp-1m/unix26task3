#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ollama_client.h"

#define HTTP_REQUEST_BUF  2048
#define HTTP_RESPONSE_BUF 16384

static int json_extract_string(const char *src,
                                const char *key,
                                char       *dest,
                                size_t      dest_size)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);

    const char *p = strstr(src, needle);
    if (!p) return 0;

    p += strlen(needle);

    size_t wi = 0;
    while (*p && wi < dest_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '"':  dest[wi++] = '"';  break;
                case '\\': dest[wi++] = '\\'; break;
                case '/':  dest[wi++] = '/';  break;
                case 'n':  dest[wi++] = '\n'; break;
                case 'r':  dest[wi++] = '\r'; break;
                case 't':  dest[wi++] = '\t'; break;
                default:   dest[wi++] = *p;   break;
            }
            p++;
        } else if (*p == '"') {
            break;
        } else {
            dest[wi++] = *p++;
        }
    }
    dest[wi] = '\0';
    return 1;
}

static void flatten_response(char *s)
{
    char *r = s, *w = s;
    int   in_space = 0;

    while (*r) {
        if (*r == '\n' || *r == '\r' || *r == '\t') {
            if (!in_space && w != s) { *w++ = ' '; }
            in_space = 1;
        } else if (*r == ' ') {
            if (!in_space && w != s) { *w++ = ' '; }
            in_space = 1;
        } else {
            *w++ = *r;
            in_space = 0;
        }
        r++;
    }
    if (w > s && *(w - 1) == ' ') w--;
    *w = '\0';
}

int ollama_generate(const char *prompt, char *response_buf, size_t buf_size)
{
    char json_body[HTTP_REQUEST_BUF];
    char escaped_prompt[HTTP_REQUEST_BUF];
    size_t ei = 0;
    for (size_t i = 0; prompt[i] && ei < sizeof(escaped_prompt) - 2; i++) {
        if (prompt[i] == '"' || prompt[i] == '\\') {
            escaped_prompt[ei++] = '\\';
        }
        escaped_prompt[ei++] = prompt[i];
    }
    escaped_prompt[ei] = '\0';

    int body_len = snprintf(json_body, sizeof(json_body),
        "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false}",
        OLLAMA_MODEL, escaped_prompt);

    if (body_len <= 0 || (size_t)body_len >= sizeof(json_body)) {
        fprintf(stderr, "OLLAMA: prompt too long to encode.\n");
        return -1;
    }

    char http_request[HTTP_REQUEST_BUF + 256];
    int  req_len = snprintf(http_request, sizeof(http_request),
        "POST /api/generate HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        OLLAMA_HOST, OLLAMA_PORT, body_len, json_body);

    if (req_len <= 0 || (size_t)req_len >= sizeof(http_request)) {
        fprintf(stderr, "OLLAMA: HTTP request buffer overflow.\n");
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("OLLAMA: socket()");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(OLLAMA_PORT);
    if (inet_pton(AF_INET, OLLAMA_HOST, &addr.sin_addr) <= 0) {
        perror("OLLAMA: inet_pton()");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("OLLAMA: connect() — is Ollama running?");
        close(sockfd);
        return -1;
    }

    if (send(sockfd, http_request, req_len, 0) < 0) {
        perror("OLLAMA: send()");
        close(sockfd);
        return -1;
    }

    char   raw[HTTP_RESPONSE_BUF];
    size_t total = 0;
    int    n;

    while (total < sizeof(raw) - 1) {
        n = recv(sockfd, raw + total, sizeof(raw) - 1 - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("OLLAMA: recv()");
            close(sockfd);
            return -1;
        }
        if (n == 0) break;
        total += n;
    }
    raw[total] = '\0';
    close(sockfd);

    if (strncmp(raw, "HTTP/", 5) != 0 || strstr(raw, "200") == NULL) {
        char status[64] = "(unknown)";
        sscanf(raw, "%*s %63[^\r\n]", status);
        fprintf(stderr, "OLLAMA: HTTP error: %s\n", status);
        return -2;
    }

    const char *body = strstr(raw, "\r\n\r\n");
    if (!body) {
        fprintf(stderr, "OLLAMA: malformed HTTP response (no header/body separator).\n");
        return -3;
    }
    body += 4;
    char llm_text[HTTP_RESPONSE_BUF];
    if (!json_extract_string(body, "response", llm_text, sizeof(llm_text))) {
        fprintf(stderr, "OLLAMA: could not find \"response\" field in: %.200s\n", body);
        return -3;
    }

    flatten_response(llm_text);

    if (strlen(llm_text) >= buf_size) {
        strncpy(response_buf, llm_text, buf_size - 4);
        response_buf[buf_size - 4] = '\0';
        strcat(response_buf, "...");
    } else {
        strncpy(response_buf, llm_text, buf_size - 1);
        response_buf[buf_size - 1] = '\0';
    }

    return 0;
}