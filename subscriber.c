#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <netinet/tcp.h>

#include "common.h"
#include "utils.h"

#define MAX_PFDS 2

void run_client(int sockfd, char *id_client) {
  char buf[SUBSCRIBER_MSG_MAXSIZE];
  char sent_buf[MSG_MAXSIZE];

  memset(buf, 0, SUBSCRIBER_MSG_MAXSIZE);
  memset(sent_buf, 0, MSG_MAXSIZE);

  struct pollfd pfds[MAX_PFDS];
  int nfds = 0;

  pfds[nfds].fd = STDIN_FILENO;
  pfds[nfds].events = POLLIN;
  nfds++;

  pfds[nfds].fd = sockfd;
  pfds[nfds].events = POLLIN;
  nfds++;

  struct packet_t sent_packet;
  struct packet_t recv_packet;

  while (1) {
    poll(pfds, nfds, -1);

    if (pfds[0].revents & POLLIN) {
      fgets(buf, sizeof(buf), stdin);

      /* Add before each message the client ID for identification */
      sprintf(sent_buf, "%s %s", id_client, buf);
      sent_packet.len = strlen(sent_buf) + 1;
      strcpy(sent_packet.message, sent_buf);

      send_all(sockfd, &sent_packet, sizeof(sent_packet));

      /* Disconnect after the exit command */
      if (strncmp(buf, EXIT, strlen(EXIT)) == 0) {
        return;
      }

    } else {
      int rc = recv_all(sockfd, &recv_packet, sizeof(recv_packet));
      
      if (rc <= 0) {
        break;
      }

      if (strncmp(recv_packet.message, EXIT, strlen(EXIT)) == 0) {
        return;
      }

      /* Show that the client subscribed to the topic */
      if (strncmp(recv_packet.message, ACK_SUBSCRIBE, strlen(ACK_SUBSCRIBE)) == 0) {
        printf("Subscribed to topic.\n");
        continue;
      }
      
      /* Show that the client unsubscribed from the topic */
      if (strncmp(recv_packet.message, ACK_UNSUBSCRIBE, strlen(ACK_UNSUBSCRIBE)) == 0) {
        printf("Unsubscribed from topic.\n");
        continue;
      }

      printf("%s\n", recv_packet.message);
    }
  }
}

int main(int argc, char *argv[]) {
  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  int sockfd = -1;

  int rc;

  if (argc != 4) {
    printf("\n Usage: %s <ID_CLIENT> <IP_SERVER> <PORT>\n", argv[0]);
    return 1;
  }

  char id_client[ID_SIZE];
  rc = sscanf(argv[1], "%s", id_client);
  DIE(rc != 1, "Given client ID is invalid");

  /* Parse the port as a number */
  uint16_t port;
  rc = sscanf(argv[3], "%hu", &port);
  DIE(rc != 1, "Given port is invalid");

  /* Obtain a STREAM socket for the TCP connection */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  DIE(sockfd < 0, "socket");

  /* Fill the required field for a TCP socket */
  struct sockaddr_in serv_addr;
  socklen_t socket_len = sizeof(struct sockaddr_in);

  memset(&serv_addr, 0, socket_len);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  rc = inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr);
  DIE(rc <= 0, "inet_pton");

  /* Connect the client to the server */
  rc = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  DIE(rc < 0, "connect");

  /* Deactivate the Nagle Algorithm */ 
  int enable = 1;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0)
    perror("setsockopt(TCP_NODELAY) failed");

  char buf[MSG_MAXSIZE];
  sprintf(buf, "%s %s", argv[1], NEW_CLIENT);

  struct packet_t sent_packet;
  struct packet_t received_packet;

  /* Initial client - server communication, announcing the ID of the client */
  sent_packet.len = strlen(buf);
  strcpy(sent_packet.message, buf);

  send_all(sockfd, &sent_packet, sizeof(sent_packet));

  recv_all(sockfd, &received_packet, sizeof(received_packet));

  /* If the client receives a denied message we cannot connect */
  if (strncmp(received_packet.message, DENIED, strlen(DENIED)) == 0) {
    close(sockfd);
    
    return 0;
  }

  run_client(sockfd, argv[1]);

  /* Close the connection on the socket */
  close(sockfd);

  return 0;
}
