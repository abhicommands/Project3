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
        error = pthread_create(&tid, NULL, read_data, con);
        if (error != 0)
        {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            close(con->fd);
            free(con);
            continue;
        }
        // automatically clean up child threads once they terminate
        pthread_detach(tid);
        // unblock handled signals
        error = pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
        if (error != 0)
        {
            fprintf(stderr, "sigmask: %s\n", strerror(error));
            exit(EXIT_FAILURE);
        }
    }
    puts("Shutting down");
    close(listener);
    // returning from main() (or calling exit()) immediately terminates all
    // remaining threads
    // to allow threads to run to completion, we can terminate the primary thread
    // without calling exit() or returning from main:
    // pthread_exit(NULL);
    // child threads will terminate once they check the value of active, but
    // there is a risk that read() will block indefinitely, preventing the
    // thread (and process) from terminating
    // to get a timely shut-down of all threads, including those blocked by
    // read(), we will could maintain a global list of all active thread IDs
    // and use pthread_cancel() or pthread_kill() to wake each one
    return EXIT_SUCCESS;
}
