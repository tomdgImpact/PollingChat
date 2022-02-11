#define _POSIX_C_SOURCE 200112L

#include "epoll-server.h"

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "connection.h"

#ifndef BACKLOG
#    define BACKLOG 15
#endif /* !BACKLOG */

#ifndef SERVER_DEBUG
#    define SERVER_DEBUG 0
#endif /* !SERVER_DEBUG */

int create_and_bind(struct addrinfo *addrinfo)
{
    struct addrinfo *info;
    int sockfd;
    for (info = addrinfo; info != NULL; info = info->ai_next)
    {
        if ((sockfd =
                 socket(info->ai_family, info->ai_socktype, info->ai_protocol))
            == -1)
        {
            continue;
        }
        int optval = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int))
            == -1)
        {
            errx(1, "Failure in setsockopt()");
        }
        if (bind(sockfd, info->ai_addr, info->ai_addrlen) == 0)
        {
            break;
        }
        close(sockfd);
    }
    if (info == NULL)
    {
        errx(1, "Could not bind");
    }
    return sockfd;
}

int prepare_socket(const char *ip, const char *port)
{
    int status = 0; // Used for error checking getaddrinfo().
    int socketfd = 0; // Socket file descriptor.
    struct addrinfo hints;
    struct addrinfo *addrinfo = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Don't mind using IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // Ensure secure data transfer.
    hints.ai_flags = AI_PASSIVE;

    // Generate the linked list of addrinfos.
    if ((status = getaddrinfo(ip, port, &hints, &addrinfo)) != 0)
    {
        errx(1, "Failure in getaddrinfo()");
    }
    // Don't forget to close the socket file descriptor.
    socketfd = create_and_bind(addrinfo);
    if (listen(socketfd, BACKLOG) == -1)
    {
        errx(1, "Failure in listen()");
    }
    return socketfd;
}

struct connection_t *accept_client(int epoll_instance, int server_socket,
                                   struct connection_t *connection)
{
    int clientfd;
    if ((clientfd = accept(server_socket, NULL, NULL)) == -1)
    {
        errx(1, "Failure in accept()");
    }
    // Register client fd in the epoll instance.
    struct epoll_event event = { 0 };
    event.data.fd = clientfd;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, clientfd, &event) == -1)
    {
        fprintf(stderr, "Failure in %s() : %s\n", __func__, strerror(errno));
        exit(1);
    }
    struct connection_t *connection_linked_list =
        add_client(connection, clientfd);
    return connection_linked_list;
}

#if SERVER_DEBUG
static void log_transit_msg(char buffer[], int n_chars_read, int client_socket)
{
    fprintf(stdout,
            "\033[0;32m[SERVER-INFO]\033[0m Received message of "
            "length [%i] from client socket [%i].\n",
            n_chars_read, client_socket);
    fprintf(stdout, "\033[0;32m[SERVER-INFO]\033[0m Message information:\n");
    fprintf(stdout, "ASCII value representation:\n");
    fprintf(stdout, "| ");
    for (int i = 0; i < n_chars_read; i++)
    {
        fprintf(stdout, "%i | ", buffer[i]);
    }
    fprintf(stdout, "\n");
    fprintf(stdout, "Character representation:\n");
    fprintf(stdout, "| ");
    for (int i = 0; i < n_chars_read; i++)
    {
        if (buffer[i] == '\n')
        {
            fprintf(stdout, "\\n | ");
        }
        else
        {
            fprintf(stdout, "%c | ", buffer[i]);
        }
    }
    fprintf(stdout, "\n");
}
#endif /* SERVER_DEBUG */

// Broadcast message from sender to all connected clients omitting the specified
// one. If no one should be ommited then call this function with -1 as
// `omit_socket_fd`.
static void broadcast(int sender_socket_fd,
                      struct connection_t connected_clients[],
                      int omit_socket_fd)
{
    // Broadcast to everyone.
    struct connection_t *sender =
        find_client(connected_clients, sender_socket_fd);
    if (sender == NULL)
    {
        fprintf(stderr,
                "[\033[0;31m[SERVER-FAILURE]\033[0m Could not find "
                "the client who sent the message in the client list.\n"
                "[client_socket:%d]\n",
                sender_socket_fd);
        fprintf(stderr,
                "\033[0;32m[SERVER-INFO]\033[0m No message has been "
                "broadcasted.\n");
        return;
    }
    if (omit_socket_fd == -1)
    {
        for (struct connection_t *cc = connected_clients; cc != NULL;
             cc = cc->next)
        {
            // TODO: secure write.
            write(cc->client_socket, sender->buffer, sender->nb_read);
        }
    }
    // Broadcast to all but one.
    else
    {
        for (struct connection_t *cc = connected_clients; cc != NULL;
             cc = cc->next)
        {
            if (cc->client_socket != omit_socket_fd)
            {
                write(cc->client_socket, sender->buffer, sender->nb_read);
            }
        }
    }
}

// Store the received message in the connection_t structure.
static void store_msg(struct connection_t *sender, char recv_msg[],
                      ssize_t recv_msg_len)
{
    if (sender == NULL)
    {
        return;
    }
    sender->buffer = realloc(sender->buffer,
                             (sender->nb_read + recv_msg_len) * sizeof(char));
    if (sender->buffer == NULL)
    {
        return;
    }
    // Append the last received message.
    memcpy(sender->buffer + sender->nb_read, recv_msg, recv_msg_len);
    // Update the length of the stored message.
    sender->nb_read += recv_msg_len;
}

static void handle_client_disconnection(int epoll_instance,
                                        struct connection_t **connected_clients,
                                        int processed_sockfd)
{
    // Broadcasting the disconnecting client's message if there's one.
    struct connection_t *disconnecting_client =
        find_client(*connected_clients, processed_sockfd);
    if (disconnecting_client->nb_read != 0)
    {
        broadcast(processed_sockfd, *connected_clients, processed_sockfd);
        fprintf(stdout,
                "\033[0;32m[SERVER-INFO]\033[0m Broadcasted the message from a "
                "disconnecting client.\n"
                "              client_file_descriptor:%d]\n",
                processed_sockfd);
    }
    // Removing client file descriptor from the interest list.
    if (epoll_ctl(epoll_instance, EPOLL_CTL_DEL, processed_sockfd, NULL) == -1)
    {
        fprintf(
            stderr,
            "[\033[0;31m[SERVER-FAILURE]\033[0m Could not remove client from "
            "the interest list.\n"
            "                 client_file_descriptor:%d]\n",
            processed_sockfd);
        fprintf(stdout, "\033[0;32m[SERVER-INFO]\033[0m Continuing execution.");
    }
    // Removing the client from the linked list structure.
    *connected_clients = remove_client(*connected_clients, processed_sockfd);
    fprintf(stdout,
            "\033[0;32m[SERVER-INFO]\033[0m Successfully removed client "
            "from the interest list.\n"
            "              [client_file_descriptor:%d]\n",
            processed_sockfd);
}

#if SERVER_DEBUG
static void
debug_print_connected_clients(struct connection_t *connected_clients)
{
    int count = 0;
    fprintf(stderr,
            "\033[0;36m======== Currently connected clients ========\033[0m\n");
    for (struct connection_t *cc = connected_clients; cc != NULL; cc = cc->next)
    {
        char printable_buffer[DEFAULT_BUFFER_SIZE] = { 0 };
        int j = 0;
        for (int i = 0; i < cc->nb_read; i++)
        {
            if (cc->buffer[i] == '\n')
            {
                printable_buffer[j++] = '\\';
                printable_buffer[j] = 'n';
            }
            else
            {
                printable_buffer[j] = cc->buffer[i];
            }
            j++;
        }
        fprintf(stderr,
                "Client [%i] information:\n"
                "                        [client_socket:%d]\n"
                "                        [client_buffer:%s]\n"
                "                        [client_nb_read:%li]\n",
                count + 1, cc->client_socket, printable_buffer, cc->nb_read);
        count++;
    }
    fprintf(stderr, "There are [%d] connected clients right now.\n", count);
    fprintf(stderr,
            "\033[0;36m======== *************************** ========\033[0m\n");
}
#endif /* SERVER_DEBUG */

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        errx(1,
             "\033[0;31m[SERVER-FAILURE]\033[0m Usage: ./epoll-server <ip> "
             "<port>. Quitting.");
    }
    int epoll_instance = epoll_create1(0);
    int server_socket = prepare_socket(argv[1], argv[2]);

    struct epoll_event event = { 0 };
    event.data.fd = server_socket;
    event.events = EPOLLIN;

    // Add the server socket file descriptor to interest list.
    if (epoll_ctl(epoll_instance, EPOLL_CTL_ADD, server_socket, &event) == -1)
    {
        errx(1,
             "[\033[0;31m[SERVER-FAILURE]\033[0m Cannot add server listening "
             "socket to interest list. Quitting.");
    }
    struct connection_t *connected_clients = NULL;
    while (1)
    {
        struct epoll_event events[MAX_EVENTS];
        int events_count = epoll_wait(epoll_instance, events, MAX_EVENTS, -1);

        // Process ready socket file descriptors.
        for (int event_idx = 0; event_idx < events_count; event_idx++)
        {
            int processed_sockfd = events[event_idx].data.fd;
            // Accepting client.
            if (processed_sockfd == server_socket)
            {
                fprintf(stdout,
                        "\033[0;32m[SERVER-INFO]\033[0m Connection established "
                        "with new client.\n");
                connected_clients = accept_client(epoll_instance, server_socket,
                                                  connected_clients);
            }
            // Communicating.
            else
            {
                // Buffer for storing received characters.
                char recv_buffer[DEFAULT_BUFFER_SIZE] = { 0 };
                // Read once to avoid blocking.
                int nr =
                    read(processed_sockfd, recv_buffer, DEFAULT_BUFFER_SIZE);
                // On EOF from client.
                if (nr == 0)
                {
                    fprintf(stdout,
                            "\033[0;32m[SERVER-INFO]\033[0m About "
                            "to disconnect client socket [%d].\n",
                            processed_sockfd);
                    handle_client_disconnection(
                        epoll_instance, &connected_clients, processed_sockfd);
                    fprintf(stdout,
                            "\033[0;32m[SERVER-INFO]\033[0m Disconnection of "
                            "client socket [%d] done successfully.\n",
                            processed_sockfd);
                }
                // Else handle the message.
                else
                {
#if SERVER_DEBUG
                    log_transit_msg(
                        recv_buffer, nr,
                        processed_sockfd); // Log messages for debug.
#endif /* SERVER_DEBUG */

                    struct connection_t *sender;
                    sender = find_client(connected_clients, processed_sockfd);
                    store_msg(sender, recv_buffer, nr);

                    // Broadcast only if you have the full message.
                    if (sender->buffer[sender->nb_read - 1] == '\n')
                    {
#if SERVER_DEBUG
                        debug_print_connected_clients(connected_clients);
#endif /* SERVER_DEBUG */
                        fprintf(stdout,
                                "\033[0;32m[SERVER-INFO]\033[0m About to "
                                "broadcast the message from client [%d].\n",
                                processed_sockfd);
                        broadcast(processed_sockfd, connected_clients, -1);
                        fprintf(stdout,
                                "\033[0;32m[SERVER-INFO]\033[0m Broadcasted "
                                "message to "
                                "all connected clients.\n");
                        sender->nb_read =
                            0; // Reset sender's buffer after broadcast.
                    }
                }
            }
        }
    }
    return 0;
}
