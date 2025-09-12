CC = gcc

# サーバー関連の設定
SERVER_TARGET = tcp_server
SERVER_SRCS = tcp_server.c error.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)

# クライアント関連の設定
CLIENT_TARGET = tcp_client
CLIENT_SRCS = tcp_client.c error.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

.PHONY: all clean

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) -o $(SERVER_TARGET)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CLIENT_OBJS) -o $(CLIENT_TARGET)

%.o: %.c
	$(CC) -c $< -o $@ -g

clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET) $(SERVER_OBJS) $(CLIENT_OBJS)