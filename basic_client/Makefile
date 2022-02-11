CC= gcc -g -fsanitize=address
# Pre-processor options (-I, include, -D ...)
CPPFLAGS = -D_POSIX_C_SOURCE=200809L #-Isrc  # -MMD may be needed # -DNDEBUG
#main compilation options
CFLAGS= -Wall -Wextra -std=c99 -pedantic -Werror
# List of source files
SRC= basic_client.c
# test source files
all: basic_client

basic_client: clean
	$(CC) $(CPPFLAGS) $(CFLAGS) -o basic_client basic_client.c

.PHONY: clean

clean:
	${RM} basic_client
