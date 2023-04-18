#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define MAX_PENDING 2
#define MAX_NAME_LEN 20
#define MAX_MSG_LEN 100

typedef struct {
  int sockfd;
  char name[MAX_NAME_LEN];
  char role;
} player_t;

int main(int argc, char const *argv[]) {
  int server_fd, new_socket, valread;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);
  char buffer[MAX_MSG_LEN] = {0};

  // create socket
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // set socket options
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("setsockopt failed");
    exit(EXIT_FAILURE);
  }

  // bind socket to port
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  // listen for incoming connections
  if (listen(server_fd, MAX_PENDING) < 0) {
    perror("listen failed");
    exit(EXIT_FAILURE);
  }

  printf("ttts server listening on port %d...\n", PORT);

  player_t player1, player2;

  // accept two players
  if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
    perror("accept failed");
    exit(EXIT_FAILURE);
  }
  printf("player 1 connected\n");

  if ((valread = read(new_socket, buffer, MAX_MSG_LEN)) <= 0) {
    perror("read failed");
    exit(EXIT_FAILURE);
  }
  player1.sockfd = new_socket;
  strncpy(player1.name, buffer, MAX_NAME_LEN);

  if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
    perror("accept failed");
    exit(EXIT_FAILURE);
  }
  printf("player 2 connected\n");

  if ((valread = read(new_socket, buffer, MAX_MSG_LEN)) <= 0) {
    perror("read failed");
    exit(EXIT_FAILURE);
  }
  player2.sockfd = new_socket;
  strncpy(player2.name, buffer, MAX_NAME_LEN);

  // randomly assign roles
  if (rand() % 2 == 0) {
    player1.role = 'X';
    player2.role = 'O';
  } else {
    player1.role = 'O';
    player2.role = 'X';
  }

  // send BEGN message to both players
  snprintf(buffer, MAX_MSG_LEN, "BEGN %c %s", player1.role, player2.name);
  write(player1.sockfd, buffer, strlen(buffer));
  snprintf(buffer, MAX_MSG_LEN, "BEGN %c %s", player2.role, player1.name);
  write(player2.sockfd, buffer, strlen(buffer));

  // main game loop
  // TODO: implement game logic and messaging

  close(player1.sockfd);
  close(player2.sockfd);
  close(server_fd);

  return 0;
}
