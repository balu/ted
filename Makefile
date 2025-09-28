ifeq ($(BINPATH),)
	BINPATH := /usr/local/bin/
endif

ifeq ($(MANPATH),)
	MANPATH := /usr/local/man/
endif

CC=gcc
CFLAGS=-std=gnu23 -Wall -Wextra -Wpedantic -Werror

.PHONY: fast small safe install

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

install:
	install -d $(BINPATH)
	install -d $(MANPATH)
	install bin/ted $(BINPATH)
	install man/ted.1 $(MANPATH)
