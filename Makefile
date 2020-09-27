# Makefile for COMP9243 Distributed Shared Memory Assignment

CC = gcc
CFLAGS = -Wall -Werror -fsanitize=address -Wshadow -std=gnu99 -g
LDFLAGS = -fsanitize=address
AR = ar

all : dsm

dsm : dsm.c
	$(CC) $(CFLAGS) -o dsm dsm.c

clean :
	rm -f *.o dsm
