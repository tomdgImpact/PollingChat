CC= gcc -g -fsanitize=address

CPPFLAGS = -D_POSIX_C_SOURCE=200112L

CFLAGS= -Wall -Wextra -std=c99 -pedantic -Werror

all: basic_server

basic_server: clean
	$(CC) $(CPPFLAGS) $(CFLAGS) -o basic_server basic_server.c

.PHONY: clean

clean:
	$(RM) basic_server
