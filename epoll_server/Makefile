CC= gcc -g -fsanitize=address

CPPFLAGS = -Iutils

CFLAGS= -Wall -Wextra -std=c99 -Werror -D_POSIX_C_SOURCE=200112L
SRC= connection.c epoll-server.c utils/xalloc.c

all: epoll_server

epoll_server: clean
	$(CC) $(CPPFLAGS) $(CFLAGS) -o epoll_server $(SRC)

.PHONY: clean

clean:
	$(RM) epoll_server
