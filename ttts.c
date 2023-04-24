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
#define MAX_CLIENTS 16
volatile int active = 1;
int num_active = 0;
int num_clients = 0;
int * gameBoard;
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
    char *name;
    int active;
    int role;
};
struct connection_data *clients[MAX_CLIENTS];
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

// initialize game board
void initBoard()
{
    gameBoard = malloc(9 * sizeof(char));
    for (int i = 0; i < 9; i++)
    {
        gameBoard[i] = '.';
    }
}

// make a function that checks if the game is over
char gameOver() // returns 'X', 'O', 'T', or '.' if game is not over
{
    // check rows
    for (int i = 0; i < 9; i += 3)
    {
        if (gameBoard[i] != '.' && gameBoard[i] == gameBoard[i + 1] && gameBoard[i + 1] == gameBoard[i + 2])
        {
            return gameBoard[i];
        }
    }
    // check columns
    for (int i = 0; i < 3; i++)
    {
        if (gameBoard[i] != '.' && gameBoard[i] == gameBoard[i + 3] && gameBoard[i + 3] == gameBoard[i + 6])
        {
            return gameBoard[i];
        }
    }
    // check diagonals
    if (gameBoard[0] != '.' && gameBoard[0] == gameBoard[4] && gameBoard[4] == gameBoard[8])
    {
        return gameBoard[0];
    }
    if (gameBoard[2] != '.' && gameBoard[2] == gameBoard[4] && gameBoard[4] == gameBoard[6])
    {
        return gameBoard[2];
    }
    // check if board is full
    for (int i = 0; i < 9; i++)
    {
        if (gameBoard[i] != '.')
        {
            return 'T';
        }
    }
    return '.';
}


#define BUFSIZE 256
#define HOSTSIZE 100
#define PORTSIZE 10
void *game(void *arg)
{// arg 
    struct thread_data *data = arg;
    // get client data
    struct connection_data *old_client_a = data->client_a;
    struct connection_data *old_client_b = data->client_b;
    // free the old clients and set the new ones
    struct connection_data *client_a = malloc(sizeof(struct connection_data));
    struct connection_data *client_b = malloc(sizeof(struct connection_data));
    char *name_a = malloc(strlen(old_client_a->name) + 1);
    char *name_b = malloc(strlen(old_client_b->name) + 1);
    strcpy(name_a, old_client_a->name);
    strcpy(name_b, old_client_b->name);
    memcpy(client_a, old_client_a, sizeof(struct connection_data));
    memcpy(client_b, old_client_b, sizeof(struct connection_data));
    free(old_client_a->name);
    free(old_client_b->name);
    free(old_client_a);
    free(old_client_b);
    free(data);

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
    FD_SET(client_a->fd, &write_fds);
    FD_SET(client_b->fd, &write_fds);

    int max_fd = (client_a->fd > client_b->fd) ? client_a->fd : client_b->fd;
    // send (BEGN|numOfBytes|player's Role| client name) to both clients
    char *msgA = malloc(100);
    int len = strlen(name_b) + 1 + 1 + 1 ;
    char role;
    if (client_a->role == 1)
        role = 'X';
    else
        role = 'O';
    sprintf(msgA, "BEGN|%d|%c|%s|\n", len, role, name_b);
    bytes_a = write(client_a->fd, msgA, strlen(msgA));
    if (bytes_a == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        fprintf(stderr, "[%s:%s] write: %s\n", host_a, port_a, strerror(errno));
    }
    free(msgA);

    char *msgB = malloc(100);
    len = strlen(name_a) + 1 + 1 + 1;
    if (client_b->role == 1)
        role = 'X';
    else
        role = 'O';
    sprintf(msgB, "BEGN|%d|%c|%s|\n", len, role, name_a);
    bytes_b = write(client_b->fd, msgB, strlen(msgB));
    if (bytes_b == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        fprintf(stderr, "[%s:%s] write: %s\n", host_b, port_b, strerror(errno));
    }
    free(msgB);

    FD_SET(client_a->fd, &read_fds);
    FD_SET(client_b->fd, &read_fds);
    // print the connection information
    printf("Game started between %s:%s and %s:%s\n", host_a, port_a, host_b, port_b);
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
            // Send message from client A to client B and client A
            bytes_b = write(client_b->fd, buf, bytes_a);
            if (bytes_b == -1)
            {
                fprintf(stderr, "[%s:%s] write: %s\n", host_b, port_b, strerror(errno));
                break;
            }
            bytes_a = write(client_a->fd, buf, bytes_a); // added line to send message back to client A
            if (bytes_a == -1)
            {
                fprintf(stderr, "[%s:%s] write: %s\n", host_a, port_a, strerror(errno));
                break;
            }
            // FD_CLR(client_a->fd, &read_fds);
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
            bytes_b = write(client_b->fd, buf, bytes_b); // added line to send message back to client B
            if (bytes_b == -1)
            {
                fprintf(stderr, "[%s:%s] write: %s\n", host_b, port_b, strerror(errno));
                break;
            }
            // FD_CLR(client_b->fd, &read_fds);
            FD_SET(client_a->fd, &write_fds);
        }
    }

    close(client_a->fd);
    close(client_b->fd);
    free(name_a);
    free(name_b);
    free(client_a);
    free(client_b);
    return NULL;
}
// find the first pair of indexes of clients that are active 
int * find_pair(struct connection_data *clients[], int num_clients)
{
    int *pair = malloc(2 * sizeof(int));
    int i, j;
    for (i = 0; i < num_clients; i++)
    {
        if ((clients[i])->active == 1)
        {
            for (j = i + 1; j < num_clients; j++)
            {
                if ((clients[j])->active == 1)
                {
                    pair[0] = i;
                    pair[1] = j;
                    return pair;
                }
            }
        }
    }
    return NULL;
}
// removes the given index of the client array and shifts the rest of the array 
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
        // if player sends "PLAY|10|Joe Smith|" and the first 4 characters are "PLAY" then, send "NAME|10|Joe Smith|\n" and in next line send "WAIT|0|\n" to player
        if (strncmp(buf, "PLAY", 4) == 0)
        {
            // send "NAME|(num_bytes)|(given name)|\n"
            char *name = strtok(buf, "|"); 
            name = strtok(NULL, "|");
            name = strtok(NULL, "|"); 
            write(con->fd, "WAIT|0|\n", 8);
            con->active = 1;
            num_active++;
            con->name = malloc(strlen(name) + 1);
            strcpy(con->name, name);
            if (num_active > 0 && (num_active % 2 == 0))
            {

                pthread_t tid;
                int error;
                sigset_t mask;
                //find the pair of clients that are active
                int *pair = find_pair(clients, num_clients);
                // make the client A's role "X" which is 1 and client B's role "O" which is 2
                (clients[pair[0]])->role = 1;
                (clients[pair[1]])->role = 2;
                //make them not active
                (clients[pair[0]])->active = 0;
                (clients[pair[1]])->active = 0;
                struct connection_data *client_a = malloc(sizeof(struct connection_data));
                struct connection_data *client_b = malloc(sizeof(struct connection_data));
                char *name_a = malloc(strlen((clients[pair[0]])->name) + 1);
                char *name_b = malloc(strlen((clients[pair[1]])->name) + 1);
                strcpy(name_a, (clients[pair[0]])->name);
                strcpy(name_b, (clients[pair[1]])->name);
                memcpy(client_a, clients[pair[0]], sizeof(struct connection_data));
                memcpy(client_b, clients[pair[1]], sizeof(struct connection_data));
                client_a->name = name_a;
                client_b->name = name_b;
                struct thread_data *thread_data = malloc(sizeof(struct thread_data));
                thread_data->client_a = client_a;
                thread_data->client_b = client_b;
                printf("Creating new thread for client pair\n");
                num_active -= 2;
                free(pair);
                error = pthread_create(&tid, NULL, game, thread_data);
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
            return NULL;
        }
        else
        {
            write(con->fd, "EROR|0|\n", 8);
        }
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

        // add the client to the array
        clients[num_clients++] = con;
        error = pthread_create(&tid, NULL, read_data, con);
        if (error != 0)
        {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            close(con->fd);
            free(con);
            continue;
        }
        // unblock handled signals
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
    // free the list of clients
    for (int i = 0; i < num_clients; i++)
    {
        if(clients[i]->name != NULL)
        {
            free(clients[i]->name);
            free(clients[i]);
        }
    }
    puts("Shutting down");
    close(listener);
    return EXIT_SUCCESS;
}
