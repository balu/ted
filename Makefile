CC=gcc
CFLAGS=-std=c23 -Wall -Wextra -Wpedantic -Werror

.PHONY: fast small safe

FAST_CFLAGS=-O3
SMALL_CFLAGS=-Os
SAFE_CFLAGS=-g -Og -fsanitize=undefined,address

.DEFAULT_GOAL := fast

fast:
	$(CC) $(CFLAGS) $(FAST_CFLAGS) -o bin/ted src/ted.c

small:
	$(CC) $(CFLAGS) $(SMALL_CFLAGS) -o bin/ted src/ted.c

safe:
	$(CC) $(CFLAGS) $(SAFE_CFLAGS) -o bin/ted src/ted.c
