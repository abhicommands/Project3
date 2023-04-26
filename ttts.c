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
#include <ctype.h>

#define QUEUE_SIZE 8
#define MAX_CLIENTS 16
volatile int active = 1;
int num_active = 0;
int num_clients = 0;
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
    char name[128];
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

// make a function that checks if the game is over
char gameOver(char *gameBoard) // returns 'X', 'O', 'D', or '.' if game is not over
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
    int count = 0;
    for (int i = 0; i < 9; i++)
    {
        if (gameBoard[i] != '.')
        {
            count++;
        }
    }
    if (count == 9)
    {
        return 'D';
    }
    return '.';
}
// check if the protocol is valid
int isValid(char *msg, int roleA, struct connection_data *client, char board[], int *isDraw)
{
    if (strncmp(msg, "RSGN", 4) == 0 || strncmp(msg, "MOVE", 4) == 0 || strncmp(msg, "DRAW", 4) == 0)
    {
        // send "NAME|(num_bytes)|(given name)|\n"
        // send the rest of the message to a function to check for error handling
        int numBytes = 0;
        int msg_error = sscanf(msg, "%*[^|]|%d|", &numBytes);
        int numDigits = 0;
        int temp = numBytes;
        while (temp != 0)
        {
            temp /= 10;
            numDigits++;
        }
        if (strncmp(msg, "RSGN", 4) == 0 && !(msg_error != 1))
        {
            if (numBytes != 0)
            {
                printf("Error: invalid message10\n");
                return 0;
            }
            if (strlen(msg) - 1 != 7)
            {
                printf("Error: invalid message30\n");
                return 0;
            }
            return 1;
        }
        if (msg_error != 1)
        {
            printf("Error: invalid message1\n");
            return 0;
        }
        else if (strlen(msg) - 1 != (6 + numDigits + numBytes))
        {
            printf("Error: invalid message2\n");
            return 0;
        }

        // Check if the message ends with a pipe character
        else if (msg[strlen(msg) - 2] != '|')
        {
            printf("Error: invalid message3\n");
            return 0;
        }
        if (strncmp(msg, "MOVE", 4) == 0)
        {
            // Extract information from the MOVE message
            int numBytes2 = 0;
            char role;
            int roleA;
            int x, y;
            int parsed = sscanf(msg, "%*[^|]|%d|%c|%d,%d|", &numBytes2, &role, &x, &y);
            if (parsed != 4 || (role != 'X' && role != 'O'))
            {
                printf("Error: INVD\n");
                return 0;
            }
            if (x < 1 || x > 3 || y < 1 || y > 3)
            {
                printf("Error: position out of bounds\n");
                return 4;
            }
            if (role == 'X')
            {
                roleA = 1;
            }
            else
            {
                roleA = 2;
            }
            // check if the client's role matches the role in the message
            if (client->role != roleA)
            {
                printf("Error: Its not your role\n");
                return 2;
            }
            // Check if the move is valid
            int index = (x - 1) * 3 + (y - 1);
            if (board[index] != '.')
            {
                printf("Error: position occupied\n");
                return 3;
            }
            if (client->role == 1)
            {
                board[index] = 'X';
            }
            else
            {
                board[index] = 'O';
            }
            return 1;
        }
        if (strncmp(msg, "DRAW", 4) == 0)
        {
            int numBytes2 = 0;
            if (strlen(msg) - 1 != 9)
            {
                printf("Error: invalid message7\n");
                return 0;
            }
            if (numBytes != 2)
            {
                printf("Error: invalid message8\n");
                return 0;
            }
            char c;
            int cerr = sscanf(msg, "%*[^|]|%d|%c|", &numBytes2, &c);
            printf("%d\n", cerr);
            printf("%c\n", c);
            if (cerr != 2 && (c == 'S' || c == 'R' || c == 'A'))
            {
                if (*isDraw == 1 && (c == 'S'))
                {
                    printf("Error: invalid message9\n");
                    return 0;
                }
                else if (*isDraw == 0 && (c == 'R' || c == 'A'))
                {
                    printf("Error: invalid message9\n");
                    return 0;
                }
                else if (*isDraw == 0 && c == 'S')
                {
                    *isDraw = 1;
                }
                else if (*isDraw == 1 && (c == 'R' || c == 'A'))
                {
                    *isDraw = 0;
                }
            }
            return 1;
        }
    }
    return 0;
}

#define BUFSIZE 256
#define HOSTSIZE 100
#define PORTSIZE 10
void *game(void *arg)
{ // arg
    struct thread_data *data = arg;
    // get client data
    struct connection_data *old_client_a = data->client_a;
    struct connection_data *old_client_b = data->client_b;
    // free the old clients and set the new ones
    struct connection_data *client_a = malloc(sizeof(struct connection_data));
    struct connection_data *client_b = malloc(sizeof(struct connection_data));
    memcpy(client_a, old_client_a, sizeof(struct connection_data));
    memcpy(client_b, old_client_b, sizeof(struct connection_data));
    free(old_client_a);
    free(old_client_b);
    free(data);
    char name_a[128], name_b[128];
    strcpy(name_a, client_a->name);
    strcpy(name_b, client_b->name);

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
    struct connection_data *moving_client = NULL;
    struct connection_data *waiting_client = NULL;
    struct connection_data *temp;
    // send (BEGN|numOfBytes|player's Role| client name) to both clients
    char *msgA = malloc(100);
    int len = strlen(name_b) + 1 + 1 + 1;
    char role;
    if (client_a->role == 1)
    {
        role = 'X';
        moving_client = client_a;
        waiting_client = client_b;
    }
    else
    {
        role = 'O';
        moving_client = client_b;
        waiting_client = client_a;
    }
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
    // initialize the game board
    char gameBoard[9];
    for (int i = 0; i < 9; i++)
    {
        gameBoard[i] = '.';
    }
    // FD_set the moving client and write INVD if the other client says something
    FD_SET(moving_client->fd, &read_fds);
    FD_SET(waiting_client->fd, &read_fds);
    printf("moving client is %s and waiting client is %s\n", moving_client->name, waiting_client->name);
    // print the connection information
    printf("Game started between %s:%s and %s:%s\n", host_a, port_a, host_b, port_b);
    int isDraw = 0;
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
        // check for data from the moving client
        if (FD_ISSET(moving_client->fd, &tmp_read_fds))
        {
            bytes_a = read(moving_client->fd, buf, BUFSIZE); // read message from the moving client
            if (bytes_a == -1)
            {
                fprintf(stderr, "[%s:%s] read: %s\n", host_a, port_a, strerror(errno));
                break;
            }
            if (bytes_a == 0)
            {
                //write the other client that the connection is closed
                char msg[256];
                sprintf(msg, "OVER|31|Connection with opponent lost.|\n");
                write(waiting_client->fd, msg, strlen(msg));
                printf("Closing connection between [%s:%s] and [%s:%s]\n", host_a, port_a, host_b, port_b);
                FD_CLR(client_a->fd, &read_fds);
                FD_CLR(client_b->fd, &write_fds);
                break;
            }
            buf[bytes_a] = '\0';
            // check if the message is valid

            int isValA = isValid(buf, bytes_a, moving_client, gameBoard, &isDraw);
            printf("[%s:%s] read %d bytes |%s|\n", host_a, port_a, bytes_a, buf);
            if (isValA != 0)
            {

                // send the message to the waiting client and the moving client in this specific format MOVD|16|X|2,2|....X....|
                char msg[256];
                if (strncmp(buf, "MOVE", 4) == 0)
                {
                    if (isValA > 1)
                    {
                        if (isValA == 2)
                        {
                            sprintf(msg, "INVL|15|Not your role.|\n");
                            write(moving_client->fd, msg, strlen(msg));
                        }
                        if (isValA == 3)
                        {
                            sprintf(msg, "INVL|24|That space is occupied.|\n");
                            write(moving_client->fd, msg, strlen(msg));
                        }
                        if (isValA == 4)
                        {
                            sprintf(msg, "INVL|27|Position is out of bounds.|\n");
                            write(moving_client->fd, msg, strlen(msg));
                        }
                    }
                    else
                    {
                        char board[10];
                        for (int i = 0; i < 9; i++)
                        {
                            board[i] = gameBoard[i];
                        }
                        board[9] = '\0';
                        char movingRole;
                        if (moving_client->role == 1)
                            movingRole = 'X';
                        else
                            movingRole = 'O';
                        sprintf(msg, "MOVD|16|%c|%s|\n", movingRole, board);
                        write(moving_client->fd, msg, strlen(msg));
                        write(waiting_client->fd, msg, strlen(msg));

                        // check if the game is over
                        char isOver = gameOver(gameBoard);
                        if (isOver == 'X')
                        {
                            sprintf(msg, "OVER|11|X|You won.|\n");
                            write(moving_client->fd, msg, strlen(msg));
                            sprintf(msg, "OVER|12|L|You lost.|\n");
                            write(waiting_client->fd, msg, strlen(msg));
                            break;
                        }
                        else if (isOver == 'O')
                        {
                            sprintf(msg, "OVER|11|O|You won.|\n");
                            write(waiting_client->fd, msg, strlen(msg));
                            sprintf(msg, "OVER|12|L|You lost.|\n");
                            write(moving_client->fd, msg, strlen(msg));
                            break;
                        }
                        else if (isOver == 'D')
                        {
                            sprintf(msg, "OVER|28|D|The game ended in a draw.|\n");
                            write(moving_client->fd, msg, strlen(msg));
                            write(waiting_client->fd, msg, strlen(msg));
                            break;
                        }
                    }
                }
                else if (strncmp(buf, "DRAW", 4) == 0)
                {
                    if (isDraw == 1)
                    {
                        sprintf(msg, "DRAW|2|S|\n");
                        write(waiting_client->fd, msg, strlen(msg));
                    }
                    else
                    {
                        if (buf[8] == 'R')
                        {
                            sprintf(msg, "DRAW|2|R|\n");
                            write(waiting_client->fd, msg, strlen(msg));
                        }
                        else
                        {
                            sprintf(msg, "OVER|28|D|The game ended in a draw.|\n");
                            write(moving_client->fd, msg, strlen(msg));
                            write(waiting_client->fd, msg, strlen(msg));
                            break;
                        }
                    }
                }
                else if (strncmp(buf, "RSGN", 4) == 0)
                {
                    sprintf(msg, "OVER|16|L|You resigned.|\n");
                    write(moving_client->fd, msg, strlen(msg));
                    int length = strlen(moving_client->name) + 17;
                    sprintf(msg, "OVER|%d|W|%s has resigned.|\n", length, moving_client->name);
                    write(waiting_client->fd, msg, strlen(msg));
                    break;
                }
                if(isValA == 1)
                {
                    // swap the moving client and the waiting client
                    temp = moving_client;
                    moving_client = waiting_client;
                    waiting_client = temp;
                }
                printf("moving client is %s and waiting client is %s\n", moving_client->name, waiting_client->name);
            }
            else
            {
                // send INVD to the moving client
                char msg[100];
                sprintf(msg, "INVL|17|Invalid command.|\n");
                write(moving_client->fd, msg, strlen(msg));
                sprintf(msg, "OVER|31|L|You put entered a bad command.|\n");
                write(moving_client->fd, msg, strlen(msg));
                sprintf(msg, "OVER|34|W|opponent entered a bad command.|\n");
                write(waiting_client->fd, msg, strlen(msg));
                break;
            }
            FD_SET(waiting_client->fd, &write_fds);
        }
        // check if waiting client says something if so, send INVD to the waiting client
        else if (FD_ISSET(waiting_client->fd, &tmp_read_fds))
        {
            bytes_b = read(waiting_client->fd, buf, BUFSIZE);
            if (bytes_b == -1)
            {
                fprintf(stderr, "[%s:%s] read: %s\n", host_b, port_b, strerror(errno));
                break;
            }
            if (bytes_b == 0)
            {
                char msg[256];
                sprintf(msg, "OVER|31|Connection with opponent lost.|\n");
                write(moving_client->fd, msg, strlen(msg));
                printf("Closing connection between [%s:%s] and [%s:%s]\n", host_a, port_a, host_b, port_b);
                FD_CLR(client_a->fd, &read_fds);
                FD_CLR(client_b->fd, &write_fds);
                break;
            }
            buf[bytes_b] = '\0';
            printf("[%s:%s] read %d bytes |%s|\n", host_b, port_b, bytes_b, buf);
            char *msg = malloc(100);
            sprintf(msg, "INVD|15|Not your turn|\n");
            bytes_b = write(waiting_client->fd, msg, strlen(msg));
            if (bytes_b == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                fprintf(stderr, "[%s:%s] write: %s\n", host_b, port_b, strerror(errno));
            }
            free(msg);
        }
        // switch the moving client and waiting client
    }

    close(client_a->fd);
    close(client_b->fd);
    free(client_a);
    free(client_b);
    return NULL;
}
// find the first pair of indexes of clients that are active
int *find_pair(struct connection_data *clients[], int num_clients)
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
            // send the rest of the message to a function to check for error handling
            int numBytes = 0;
            int msg_error = sscanf(buf, "%*[^|]|%d|", &numBytes);
            int numDigits = 0;
            int temp = numBytes;
            while (temp != 0)
            {
                temp /= 10;
                numDigits++;
            }
            if (msg_error != 1)
            {
                printf("Invalid message: missing or invalid byte count\n");
                char msg[100];
                sprintf(msg, "INVL|17|Invalid command.|\n");
                write(con->fd, msg, strleng(msg));
            }
            else if (strlen(buf) - 1 != (6 + numDigits + numBytes))
            {
                char msg[100];
                sprintf(msg, "INVL|17|Invalid command.|\n");
                write(con->fd, msg, strleng(msg));
            }

            // Check if the message ends with a pipe character
            else if (buf[strlen(buf) - 2] != '|')
            {
                char msg[100];
                sprintf(msg, "INVL|17|Invalid command.|\n");
                write(con->fd, msg, strleng(msg));
            }
            else
            {
                char *name = strtok(buf, "|");
                name = strtok(NULL, "|");
                name = strtok(NULL, "|");
                con->active = 1;
                num_active++;
                strcpy(con->name, name);
                if (num_active > 0 && (num_active % 2 == 0))
                {
                    pthread_t tid;
                    int error;
                    sigset_t mask;
                    // find the pair of clients that are active
                    int *pair = find_pair(clients, num_clients);
                    // make the client A's role "X" which is 1 and client B's role "O" which is 2
                    (clients[pair[0]])->role = 1;
                    (clients[pair[1]])->role = 2;
                    // make them not active
                    (clients[pair[0]])->active = 0;
                    (clients[pair[1]])->active = 0;
                    // check if the names are the same and if they are, then send "EROR|0|ERROR D\n" to both clients and close the connection
                    if (strcmp((clients[pair[0]])->name, (clients[pair[1]])->name) == 0)
                    {
                        // send same name error to client B
                        printf("Same name error\n");
                        char msg[100];
                        sprintf(msg, "INVL|17|Invalid command.|\n");
                        write((clients[pair[1]])->fd, msg, strlen(msg));
                        close((clients[pair[1]])->fd);
                        (clients[pair[0]])->active = 1;
                        num_active--;
                        free(pair);
                        return NULL;
                    }
                    struct connection_data *client_a = malloc(sizeof(struct connection_data));
                    struct connection_data *client_b = malloc(sizeof(struct connection_data));
                    memcpy(client_a, clients[pair[0]], sizeof(struct connection_data));
                    memcpy(client_b, clients[pair[1]], sizeof(struct connection_data));
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
                write(con->fd, "WAIT|0|\n", 8);
                return NULL;
            }
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
        close(clients[i]->fd);
        free(clients[i]);
    }
    puts("Shutting down");
    close(listener);
    return EXIT_SUCCESS;
}
