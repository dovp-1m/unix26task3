CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11
LDFLAGS =
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = .

SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/config_parser.c $(SRC_DIR)/irc_client.c $(SRC_DIR)/bot_logic.c $(SRC_DIR)/narrative_parser.c
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

TARGET = $(BIN_DIR)/irc_chatbot

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
