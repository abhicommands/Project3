// NOTE: must use option -pthread when compiling!
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>

#define QUEUE_SIZE 8
volatile int active = 1;
void handler(int signum)
{
    active = 0;
}
// set up signal handlers for primary thread
// return a mask blocking those signals for worker threads
// FIXME should check whether any of these actually succeeded
void install_handlers(sigset_t *mask)
{
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigaddset(mask, SIGTERM);
}
// data to be sent to worker threads
struct connection_data
{
    struct sockaddr_storage addr;
    socklen_t addr_len;
    int fd;
};
struct thread_data
{
    struct connection_data *client_a;
    struct connection_data *client_b;
};
int open_listener(char *service, int queue_size)
{
    struct addrinfo hint, *info_list, *info;
    int error, sock;
    // initialize hints
    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;
    // obtain information for listening socket
    error = getaddrinfo(NULL, service, &hint, &info_list);
    if (error)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }
    // attempt to create socket
    for (info = info_list; info != NULL; info = info->ai_next)
    {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        // if we could not create the socket, try the next method
        if (sock == -1)
            continue;
        // bind socket to requested port
        error = bind(sock, info->ai_addr, info->ai_addrlen);
        if (error)
        {
            close(sock);
            continue;
        }
        // enable listening for incoming connection requests
        error = listen(sock, queue_size);
        if (error)
        {
            close(sock);
            continue;
        }
        // if we got this far, we have opened the socket
        break;
    }
    freeaddrinfo(info_list);
    // info will be NULL if no method succeeded
    if (info == NULL)
    {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }
    return sock;
}
#define BUFSIZE 256
#define HOSTSIZE 100
#define PORTSIZE 10
void *read_data(void *arg)
{
    struct connection_data *con = arg;
    char buf[BUFSIZE + 1], host[HOSTSIZE], port[PORTSIZE];
    int bytes, error;
    error = getnameinfo(
        (struct sockaddr *)&con->addr, con->addr_len,
        host, HOSTSIZE,
        port, PORTSIZE,
        NI_NUMERICSERV);
    if (error)
    {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
        strcpy(host, "??");
        strcpy(port, "??");
    }
    printf("Connection from %s:%s\n", host, port);
    while (active && (bytes = read(con->fd, buf, BUFSIZE)) > 0)
    {
        buf[bytes] = '\0';
        printf("[%s:%s] read %d bytes |%s|\n", host, port, bytes, buf);
        write(con->fd, buf, bytes);
    }
    if (bytes == 0)
    {
        printf("[%s:%s] got EOF\n", host, port);
    }
    else if (bytes == -1)
    {
        printf("[%s:%s] terminating: %s\n", host, port, strerror(errno));
    }
    else
    {
        printf("[%s:%s] terminating\n", host, port);
    }
    close(con->fd);
    free(con);
    return NULL;
}
void *chat_thread(void *arg)
{
    struct thread_data *data = (struct thread_data *)arg;
    struct connection_data *client_a = data->client_a;
    struct connection_data *client_b = data->client_b;
    char buf[BUFSIZE + 1], host_a[HOSTSIZE], port_a[PORTSIZE], host_b[HOSTSIZE], port_b[PORTSIZE];
    int bytes_a, bytes_b, client_a_err, client_b_err;
    client_a_err = getnameinfo(
        (struct sockaddr *)&client_a->addr, client_a->addr_len,
        host_a, HOSTSIZE,
        port_a, PORTSIZE,
        NI_NUMERICSERV);
    if (client_a_err)
    {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(client_a_err));
        strcpy(host_a, "??");
        strcpy(port_a, "??");
    }
    client_b_err = getnameinfo(
        (struct sockaddr *)&client_b->addr, client_b->addr_len,
        host_b, HOSTSIZE,
        port_b, PORTSIZE,
        NI_NUMERICSERV);
    if (client_b_err)
    {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(client_b_err));
        strcpy(host_b, "??");
        strcpy(port_b, "??");
    }
    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_SET(client_a->fd, &read_fds);
    FD_SET(client_b->fd, &read_fds);
    int max_fd = (client_a->fd > client_b->fd) ? client_a->fd : client_b->fd;
    while (active)
    {
        fd_set tmp_read_fds = read_fds;
        fd_set tmp_write_fds = write_fds;
        int num_ready = select(max_fd + 1, &tmp_read_fds, &tmp_write_fds, NULL, NULL);
        if (num_ready == -1)
        {
            fprintf(stderr, "select: %s\n", strerror(errno));
            break;
        }
        // Check for data to read from client A
        if (FD_ISSET(client_a->fd, &tmp_read_fds))
        {
            bytes_a = read(client_a->fd, buf, BUFSIZE);
            if (bytes_a == -1)
            {
                fprintf(stderr, "[%s:%s] read: %s\n", host_a, port_a, strerror(errno));
                break;
            }
            if (bytes_a == 0)
            {
                printf("Closing connection between [%s:%s] and [%s:%s]\n", host_a, port_a, host_b, port_b);
                FD_CLR(client_a->fd, &read_fds);
                FD_CLR(client_b->fd, &write_fds);
                break;
            }
            buf[bytes_a] = '\0';
            printf("[%s:%s] read %d bytes |%s|\n", host_a, port_a, bytes_a, buf);
            // Send message from client A to client B
            bytes_b = write(client_b->fd, buf, bytes_a);
            if (bytes_b == -1)
            {
                fprintf(stderr, "[%s:%s] write: %s\n", host_b, port_b, strerror(errno));
                break;
            }
            FD_CLR(client_a->fd, &read_fds);
            FD_SET(client_b->fd, &write_fds);
        }

        // Check for data to read from client B
        if (FD_ISSET(client_b->fd, &tmp_read_fds))
        {
            bytes_b = read(client_b->fd, buf, BUFSIZE);
            if (bytes_b == -1)
            {
                fprintf(stderr, "[%s:%s] read: %s\n", host_b, port_b, strerror(errno));
                break;
            }
            if (bytes_b == 0)
            {
                printf("Closing connection between [%s:%s] and [%s:%s]\n", host_a, port_a, host_b, port_b);
                FD_CLR(client_b->fd, &read_fds);
                FD_CLR(client_a->fd, &write_fds);
                break;
            }
            buf[bytes_b] = '\0';
            printf("[%s:%s] read %d bytes |%s|\n", host_b, port_b, bytes_b, buf);
            // Send message from client B to client A
            bytes_a = write(client_a->fd, buf, bytes_b);
            if (bytes_a == -1)
            {
                fprintf(stderr, "[%s:%s] write: %s\n", host_a, port_a, strerror(errno));
                break;
            }
            FD_CLR(client_b->fd, &read_fds);
            FD_SET(client_a->fd, &write_fds);
        }
    }

    close(client_a->fd);
    close(client_b->fd);
    free(client_a);
    free(client_b);
    return NULL;
}
int main(int argc, char **argv)
{
    sigset_t mask;
    struct connection_data *con;
    int error;
    pthread_t tid;
    char *service = argc == 2 ? argv[1] : "15000";
    install_handlers(&mask);
    int listener = open_listener(service, QUEUE_SIZE);
    if (listener < 0)
        exit(EXIT_FAILURE);
    printf("Listening for incoming connections on %s\n", service);

    // maintain a list of connected clients
    struct connection_data *clients[QUEUE_SIZE];
    int num_clients = 0;

    while (active)
    {
        con = (struct connection_data *)malloc(sizeof(struct connection_data));
        con->addr_len = sizeof(struct sockaddr_storage);
        con->fd = accept(listener,
                         (struct sockaddr *)&con->addr,
                         &con->addr_len);
        if (con->fd < 0)
        {
            perror("accept");
            free(con);
            // TODO check for specific error conditions
            continue;
        }
        // temporarily disable signals
        // (the worker thread will inherit this mask, ensuring that SIGINT is
        // only delivered to this thread)
        error = pthread_sigmask(SIG_BLOCK, &mask, NULL);
        if (error != 0)
        {
            fprintf(stderr, "sigmask: %s\n", strerror(error));
            exit(EXIT_FAILURE);
        }

        // add the new client to the list
        clients[num_clients++] = con;

        // if there are two clients available, pair them and create a new thread
        if (num_clients % 2 == 0)
        {
            struct connection_data *client_a = clients[num_clients - 2];
            struct connection_data *client_b = clients[num_clients - 1];

            // create a new thread for the client pair
            struct thread_data *thread_data = malloc(sizeof(struct thread_data));
            thread_data->client_a = client_a;
            thread_data->client_b = client_b;
            printf("Creating new thread for client pair\n");
            error = pthread_create(&tid, NULL, chat_thread, thread_data);
            if (error != 0)
            {
                fprintf(stderr, "pthread_create: %s\n", strerror(error));
                close(client_a->fd);
                close(client_b->fd);
                free(client_a);
                free(client_b);
                free(thread_data);
                continue;
            }

            // remove the clients from the list
            clients[num_clients - 2] = NULL;
            clients[num_clients - 1] = NULL;
            num_clients -= 2;

            // automatically clean up the thread once it terminates
            pthread_detach(tid);
            // unblock handled signals
            error = pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
            if (error != 0)
            {
                fprintf(stderr, "sigmask: %s\n", strerror(error));
                exit(EXIT_FAILURE);
            }
        }
    }

    // clean up any remaining clients
    for (int i = 0; i < num_clients; i++)
    {
        close(clients[i]->fd);
        free(clients[i]);
    }

    puts("Shutting down");
    close(listener);
    return EXIT_SUCCESS;
}
