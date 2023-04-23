CFLAGS = -pthread -g -std=c99 -Wall -fsanitize=address,undefined
CC = gcc

server: ttts.c
	$(CC) $(CFLAGS) -o ttts ttts.c
	mv ttts ./bin

client: ttt.c
	$(CC) -o ttt ttt.c
	mv ttt ./bin

chatserver: chats.c
	$(CC) $(CFLAGS) -o chats chats.c
	mv chats ./bin

chatclient: chatc.c
	$(CC) $(CFLAGS) -o chatc chatc.c
	mv chatc ./bin
