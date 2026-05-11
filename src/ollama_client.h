#ifndef OLLAMA_CLIENT_H
#define OLLAMA_CLIENT_H

#define OLLAMA_HOST "127.0.0.1"
#define OLLAMA_PORT 11434
#define OLLAMA_MODEL "tinyllama"

#define OLLAMA_RESPONSE_MAX 450

int ollama_generate(const char *prompt, char *response_buf, size_t buf_size);

#endif