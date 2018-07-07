CC = gcc
DEBUG = -g
CFLAGS = -Wall -march=x86-64 $(DEBUG)
LFLAGS = -Wall $(DEBUG)

ping_pong: ping_pong.c
	$(CC) $(CFLAGS) ping_pong.c -o ping_pong
