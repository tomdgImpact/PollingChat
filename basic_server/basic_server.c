#include "basic_server.h"

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
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(ip, port, &hints, &addr) != 0)
        errx(EXIT_FAILURE, "fail getting address");

    int sockfd = create_and_bind(addr);
    if (listen(sockfd, 10) == -1)
        errx(1, "cannot listen on this socket");
    return sockfd;
}

int create_and_bind(struct addrinfo *addrinfo)
{
    int sockfd = 0;
    struct addrinfo *cur = NULL;

    for (cur = addrinfo; cur != NULL; cur = cur->ai_next)
    {
        sockfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sockfd == -1)
            continue;
        int enable = 1;
        int ers =
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        if (ers == -1)
            errx(1, "set socketoption failed");
        if (bind(sockfd, cur->ai_addr, cur->ai_addrlen) != -1)
            break;

        close(sockfd);
    }

    freeaddrinfo(addrinfo);
    if (cur == NULL)
        errx(EXIT_FAILURE, "Couldn't connect to remote server");

    return sockfd;
}

int accept_client(int socket)
{
    int sfd_client = accept(socket, NULL, NULL);
    if (sfd_client == -1)
        errx(1, "Connect client to server failed");
    printf("Client connected\n");
    return sfd_client;
}

static void rewrite(char *buf, int size)
{
    int w_check = 0;
    write(STDOUT_FILENO, "Received Body: ", 15);
    while ((w_check = write(STDOUT_FILENO, buf, size)) != 0)
    {
        if (w_check == -1)
            errx(1, "cannot write body on stdout");
        buf += w_check;
        size -= w_check;
    }
}

static void print_and_send(char *pre_print, size_t size, int client_socket)
{
    int w = 0;
    rewrite(pre_print, size);
    while ((w = send(client_socket, pre_print, size, MSG_NOSIGNAL)) != 0)
    {
        if (w == -1)
            return;
        pre_print += w;
        size -= w;
        if (size == 0)
            break;
    }
}

void communicate(int client_socket)
{
    int r_check = 1;
    size_t size = DEFAULT_BUFFER_SIZE;
    char *buf = calloc(size, sizeof(char));
    if (buf == NULL)
        errx(1, "memory allocation failed");
    size_t big_size = 4096;
    char *pre_print = calloc(big_size, sizeof(char));
    size_t cur_len = 0;
    if (pre_print == NULL)
        errx(1, "memory allocation fail in %s", __func__);
    while (r_check != 0 && r_check != -1 && r_check != EOF)
    {
        while ((r_check = read(client_socket, buf, size)) != 0)
        {
            if (r_check == -1)
                errx(1, "cannot read from client");
            if (pre_print[big_size - 1] != '\0'
                || strlen(pre_print) + r_check > big_size)
            {
                big_size *= 2;
                pre_print = realloc(pre_print, big_size * sizeof(char));
                if (pre_print == NULL)
                    return;
            }
            pre_print = strcat(pre_print, buf);
            cur_len += r_check;
            memset(buf, 0, size);
            if (pre_print[cur_len - 1] == '\n')
                break;
        }
        if (cur_len == 0)
            break;
        if (pre_print[cur_len - 1] == '\n')
        {
            print_and_send(pre_print, cur_len, client_socket);
            memset(pre_print, 0, big_size);
        }
        if (r_check == 0)
            break;
        if (fcntl(client_socket, F_GETFL) == -1 || errno == EBADF)
            break;
        cur_len = 0;
    }
    free(pre_print);
    free(buf);
    close(client_socket);
    write(STDOUT_FILENO, "Client disconnected\n", 20);
}

int main(int argc, char **argv)
{
    if (argc != 3)
        fprintf(stderr, "Usage : ./basic_server ip_address port\n");
    int sockfd = prepare_socket(argv[1], argv[2]);
    while (1)
    {
        int client_fd = accept_client(sockfd);
        communicate(client_fd);
    }
    close(sockfd);
    return 0;
}
