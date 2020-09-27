# Makefile for COMP9243 Distributed Shared Memory Assignment

CC = gcc
CFLAGS = -Wall -Werror -std=gnu99 -g
AR = ar

all : dsm

dsm : dsm.c
	$(CC) $(CFLAGS) -o dsm dsm.c

clean :
	rm -f *.o dsm
