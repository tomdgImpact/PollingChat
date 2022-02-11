#include "basic_client.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int prepare_socket(const char *ip, const char *port)
{
    struct addrinfo *addr = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip, port, &hints, &addr) != 0)
        errx(EXIT_FAILURE, "fail getting address");

    return create_and_connect(addr);
}

int create_and_connect(struct addrinfo *addrinfo)
{
    int sockfd = 0;
    struct addrinfo *cur = NULL;

    for (cur = addrinfo; cur != NULL; cur = cur->ai_next)
    {
        sockfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sockfd == -1)
            continue;

        if (connect(sockfd, cur->ai_addr, cur->ai_addrlen) != -1)
            break;

        close(sockfd);
    }

    freeaddrinfo(addrinfo);
    if (cur == NULL)
        errx(EXIT_FAILURE, "Couldn't connect to remote server");

    return sockfd;
}

static void sending(int sockfd, char *buf, int nbread)
{
    int check = 0;

    while ((check = send(sockfd, buf, nbread, 0)) != 0)
    {
        if (check == -1)
            errx(EXIT_FAILURE, "cannot send data");

        buf += check;
        nbread -= check;
    }
}

static int reading(int server_socket, int size)
{
    int r_check = 0;
    char *line = NULL;
    size_t line_size = size;

    while ((r_check = getline(&line, &line_size, stdin)) != 0)
    {
        if (r_check == -1)
            errx(EXIT_FAILURE, "cannot read terminal input");
        if (r_check != 0)
        {
            sending(server_socket, line, r_check);
            if (line[r_check - 1] == '\n')
                break;
            memset(line, 0, size);
        }
    }
    return r_check;
}

void communicate(int server_socket)
{
    int size = DEFAULT_BUFFER_SIZE;
    char *buf = calloc(size, sizeof(char));
    if (buf == NULL)
        errx(EXIT_FAILURE, "cannot allocate memory");
    int r_check = 0;
    while (fcntl(STDIN_FILENO, F_GETFL) != -1 || errno != EBADF)
    {
        fprintf(stderr, "Enter your message:\n");
        r_check = reading(server_socket, size);
        if (r_check == 0)
            break;
        int receiv = 0;
        memset(buf, 0, size);
        write(STDOUT_FILENO, "Server answered with: ", 22);
        while ((receiv = read(server_socket, buf, size)) != 0)
        {
            if (receiv == -1)
                errx(EXIT_FAILURE, "failed to receive data");
            write(STDOUT_FILENO, buf, receiv);
            if (buf[receiv - 1] == '\n')
                break;
            memset(buf, 0, size);
        }
    }
    free(buf);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: ./basic_client SERVER_IP SERVER_PORT");
        return 1;
    }
    int sockfd = prepare_socket(argv[1], argv[2]);
    communicate(sockfd);
    return 0;
}
