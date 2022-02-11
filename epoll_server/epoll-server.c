#include "epoll-server.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

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

struct connection_t *accept_client(int epli, int serv_fd,
                                   struct connection_t *connection)
{
    int sfd_client = accept(serv_fd, NULL, NULL);
    if (sfd_client == -1)
        return NULL;
    printf("Client connected\n");
    connection = add_client(connection, sfd_client);
    struct epoll_event evt;
    evt.data.fd = sfd_client;
    evt.events = EPOLLIN;
    if (epoll_ctl(epli, EPOLL_CTL_ADD, sfd_client, &evt) == -1)
        errx(1, "cannot add to epoll instancd client fd");
    return connection;
}

static void Networks(int sender_fd, struct connection_t *clients, int pass)
{
    struct connection_t *in = find_client(clients, sender_fd);
    if (in == NULL)
        return;
    if (pass == -1)
    {
        for (struct connection_t *cc = clients; cc != NULL; cc = cc->next)
            send(cc->client_socket, in->buffer, in->nb_read, MSG_NOSIGNAL);
    }
    else
    {
        for (struct connection_t *cc = clients; cc != NULL; cc = cc->next)
        {
            if (cc->client_socket != pass)
                send(cc->client_socket, in->buffer, in->nb_read, MSG_NOSIGNAL);
        }
    }
}

void save_data(struct connection_t *in, char *recv_msg, size_t len)
{
    if (in == NULL)
        return;
    in->buffer = realloc(in->buffer, (in->nb_read + len) * sizeof(char));
    if (in->buffer == NULL)
        return;
    memcpy(in->buffer + in->nb_read, recv_msg, len);
    in->nb_read += len;
}

static void disconnect(int epli, struct connection_t **clients, int cur_fd)
{
    struct connection_t *disconnecting_client = find_client(*clients, cur_fd);
    if (disconnecting_client->nb_read != 0)
        Networks(cur_fd, *clients, cur_fd);
    epoll_ctl(epli, EPOLL_CTL_DEL, cur_fd, NULL);
    *clients = remove_client(*clients, cur_fd);
    printf("Client disconnected\n");
}

static void communicate(int epli, int serv_fd)
{
    struct connection_t *clients = NULL;
    while (1)
    {
        struct epoll_event events[MAX_EVENTS];
        int events_count = epoll_wait(epli, events, MAX_EVENTS, -1);

        for (int index = 0; index < events_count; index++)
        {
            int cur_fd = events[index].data.fd;
            if (cur_fd == serv_fd)
                clients = accept_client(epli, serv_fd, clients);
            else
            {
                char recv_buffer[DEFAULT_BUFFER_SIZE];
                int nr = recv(cur_fd, recv_buffer, DEFAULT_BUFFER_SIZE, 0);
                if (nr == 0)
                    disconnect(epli, &clients, cur_fd);
                else
                {
                    struct connection_t *in = find_client(clients, cur_fd);
                    save_data(in, recv_buffer, nr);
                    if (in->buffer[in->nb_read - 1] == '\n')
                    {
                        Networks(cur_fd, clients, -1);
                        in->nb_read = 0;
                    }
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage : ./epoll_server ip_address port\n");
        return 1;
    }
    int serv_fd = prepare_socket(argv[1], argv[2]);

    int epli = epoll_create1(0);
    struct epoll_event event = { 0 };
    event.data.fd = serv_fd;
    event.events = EPOLLIN;

    if (epoll_ctl(epli, EPOLL_CTL_ADD, serv_fd, &event) == -1)
        errx(1, "cannot add socket to epoll");

    communicate(epli, serv_fd);
    return 0;
}