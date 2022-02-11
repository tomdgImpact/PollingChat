#include "epoll-server.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "utils/xalloc.h"

static void free_connection(struct connection_t *connect)
{
    if (connect->next != NULL)
    {
        for (struct connection_t *c = connect; c != NULL;)
        {
            struct connection_t *tmp = c;
            c = c->next;
            free(tmp->buffer);
            free(tmp);
        }
    }
    free(connect);
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

struct connection_t *accept_client(int epoll_instance, int server_socket,
                                   struct connection_t *connection)
{
    int sfd_client = accept(server_socket, NULL, NULL);
    if (sfd_client == -1)
        return NULL;
    printf("Client connected\n");
    connection = add_client(connection, sfd_client);
    struct epoll_event evt;
    // we may add this line if it the program don't run'
    evt.data.fd = sfd_client;
    evt.events = EPOLLIN;
    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, sfd_client, &evt) == -1)
        errx(1, "cannot add to epoll instancd client fd");
    return connection;
}

static struct connection_t *fil_buf(struct connection_t *c, char *b, int nbread)
{
    if (nbread == 0)
        return c;
    if (c->buffer == NULL)
    {
        c->buffer = calloc(nbread + 1, sizeof(char));
        if (c->buffer == NULL)
            errx(1, "cannot allocate memory");
        c->nb_read = nbread + 1;
        c->buffer = strcpy(c->buffer, b);
    }
    else
    {
        c->buffer =
            realloc(c->buffer, (c->nb_read + nbread + 1) * sizeof(char));
        if (c->buffer == NULL)
            errx(1, "realloc failed");
        c->buffer = strcat(c->buffer, b);
        c->nb_read += nbread + 1;
    }
    return c;
}

static void chat(int clfd, struct connection_t *co, struct connection_t *full_c)
{
    int nbread = 0;
    char *buf = calloc(DEFAULT_BUFFER_SIZE, sizeof(char));
    nbread = recv(clfd, buf, DEFAULT_BUFFER_SIZE, 0);
    if (nbread == -1)
        return;
    co = fil_buf(co, buf, nbread);
    /* echo the whole msg*/
    int sending = 0;
    if (nbread != 0 && buf[nbread - 1] != '\n')
    {
        free(buf);
        return;
    }
    for (struct connection_t *cur = full_c; cur != NULL; cur = cur->next)
    {
        char *keep_ptr = co->buffer;
        ssize_t keep_nb = co->nb_read;
        if (nbread == 0 && cur->client_socket == clfd)
            continue;
        while (1)
        {
            sending =
                send(cur->client_socket, keep_ptr, keep_nb - 1, MSG_NOSIGNAL);
            if (sending == 0 || sending == -1)
                break;
            keep_ptr += sending;
            keep_nb -= sending;
            if (keep_nb == 0)
                break;
        }
    }
    if (nbread == 0)
    {
        printf("Client deconnected\n");
        full_c = remove_client(full_c, clfd);
        close(clfd);
    }
    if (nbread != 0 && co)
    {
        memset(co->buffer, 0, co->nb_read);
        co->nb_read = 0;
    }
    free(buf);
}

void communicate(int epoll_inst, int sockfd_server, struct connection_t *con)
{
    int size_buf = DEFAULT_BUFFER_SIZE;
    char *buf = xcalloc(size_buf, sizeof(char));
    if (buf == NULL)
        errx(1, "memory allocation");
    while (1)
    {
        struct epoll_event events[MAX_EVENTS];
        int events_count = epoll_wait(epoll_inst, events, MAX_EVENTS, -1);
        for (int event_id = 0; event_id < events_count; event_id++)
        {
            /* try to handle the ready list */
            int ready_sock = events[event_id].data.fd;
            if (ready_sock == sockfd_server)
            {
                con = accept_client(epoll_inst, sockfd_server, con);
            }
            else
            {
                // find the client that want to communicate and do the echo chat
                int client = events[event_id].data.fd;
                struct connection_t *cli_con = find_client(con, client);
                chat(client, cli_con, con);
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
    int sockfd_server = prepare_socket(argv[1], argv[2]);
    int epoll_instance = epoll_create1(0);
    struct epoll_event event = { 0 };
    event.data.fd = sockfd_server;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, sockfd_server, &event) == -1)
        errx(1, "fail to add socket to epoll instance");
    struct connection_t *connect = xcalloc(1, sizeof(struct connection_t));
    if (connect == NULL)
        errx(1, "cannot allocate memeory");
    communicate(epoll_instance, sockfd_server, connect);
    free_connection(connect);
    return 0;
}
