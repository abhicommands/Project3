#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>

#define BUFLEN 256

int connect_inet(char *host, char *service)
{
    struct addrinfo hints, *info_list, *info;
    int sock, error;

    // look up remote host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;  // in practice, this means give us IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // indicate we want a streaming socket

    error = getaddrinfo(host, service, &hints, &info_list);
    if (error) {
        fprintf(stderr, "error looking up %s:%s: %s\n", host, service, gai_strerror(error));
        return -1;
    }

    for (info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (sock < 0) continue;

        error = connect(sock, info->ai_addr, info->ai_addrlen);
        if (error) {
            close(sock);
            continue;
        }

        break;
    }
    freeaddrinfo(info_list);

    if (info == NULL) {
        fprintf(stderr, "Unable to connect to %s:%s\n", host, service);
        return -1;
    }

    return sock;
}

void *chat_thread(void *arg) {
    int sock = *(int *)arg;
    char buf[BUFLEN];
    int read_bytes, write_bytes;
    while ((read_bytes = read(sock, buf, BUFLEN)) > 0) {
        write_bytes = write(STDOUT_FILENO, buf, read_bytes);
        if (write_bytes != read_bytes) {
            fprintf(stderr, "Error writing to stdout\n");
            exit(EXIT_FAILURE);
        }
    }
    close(sock);
    return NULL;
}

int main(int argc, char **argv)
{
    int sock;
    char buf[BUFLEN];
    pthread_t tid;
    if (argc != 3) {
        printf("Specify host and service\n");
        exit(EXIT_FAILURE);
    }
    sock = connect_inet(argv[1], argv[2]);
    if (sock < 0) exit(EXIT_FAILURE);
    pthread_create(&tid, NULL, chat_thread, &sock);
    int read_bytes;
    int write_bytes;
    while ((read_bytes = read(STDIN_FILENO, buf, BUFLEN)) > 0) {
        write_bytes = write(sock, buf, read_bytes);
        if (write_bytes != read_bytes) {
            fprintf(stderr, "Error writing to socket\n");
            exit(EXIT_FAILURE);
        }
    }
    close(sock);
    pthread_join(tid, NULL);
    return EXIT_SUCCESS;
}
