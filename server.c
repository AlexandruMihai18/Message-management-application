#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <netinet/tcp.h>

#include "common.h"
#include "utils.h"
#include "hashtable.h"

#define MAX_CONNECTIONS 100
#define IP_SERVER "0.0.0.0"

#define MAX_PAYLOAD_SIZE 1500

hashtable_t *subscribers_ht;
hashtable_t *topics_ht;

/* Send ack message to a TCP client for either SUBSCRIBE or UNSUBCRIBE */
void send_ack(int sockfd, char *ack) {
  struct packet_t sent_packet;
  sent_packet.len = strlen(ack);
  strcpy(sent_packet.message, ack);

  int rc = send_all(sockfd, &sent_packet, sizeof(sent_packet));
  DIE (rc < 0, "send all");
}

/* Send to a TCP client all messages on a topic starting from the nth */
void send_from_nth_msg(int sockfd, char *topic, int *n) {
  struct topic_t *topic_entry = ht_get(topics_ht, topic);
  ll_node_t *current_message = topic_entry->contents->head;
  
  for (int i = 0; i < *n; i++) {
    current_message = current_message->next;
  }

  struct packet_t sent_packet;

  while (current_message) {
    sent_packet.len = strlen(current_message->data);
    strcpy(sent_packet.message, current_message->data);
    send_all(sockfd, &sent_packet, sizeof(sent_packet));

    current_message = current_message->next;
  }

  /* Update the last message send by overwriting the n value */
  *n = topic_entry->contents->size;
}

/* Check if a client has previously subscribed to a topic */
int check_for_previous_subscription(char *id_client, char *topic, int sf) {
  struct subscriber_t *sub_entry = ht_get(subscribers_ht, id_client);

  ll_node_t *current_topic = sub_entry->topics->head;
  struct topic_status_t *topic_status;

  while (current_topic) {
    topic_status = current_topic->data;

    /* If the client was previously connected and SF is 1, send all the messages */
    if (strcmp(topic_status->topic, topic) == 0) {
      if (topic_status->status == 1) {
        send_from_nth_msg(sub_entry->sockfd, topic, &(topic_status->last_msg));
      }

      topic_status->status = sf;

      struct topic_t *topic_entry = ht_get(topics_ht, topic);

      /* Mark the client as subscribed */
      ll_add_nth_node(topic_entry->subscribers, 0, id_client);

      send_ack(sub_entry->sockfd, ACK_SUBSCRIBE);

      return 1;
    }

    current_topic = current_topic->next;
  }

  return 0;
}

/* Send messages for a reconnected client */
void send_msg_reconnected(struct subscriber_t *subscriber) {
  ll_node_t *current_topic = subscriber->topics->head;
  struct topic_status_t *topic_status;

  struct topic_t *topic;

  while (current_topic) {
    topic_status = current_topic->data;
    
    /* If the SF field is 1 we retransmit the messages */
    if (topic_status->status == 1) {
      send_from_nth_msg(subscriber->sockfd, topic_status->topic, &(topic_status->last_msg));
    }

    topic = ht_get(topics_ht, topic_status->topic);

    topic_status->last_msg = topic->contents->size; 

    current_topic = current_topic->next;
  }
}

/* Remove client subscription from a certain topic */
void remove_client_subscription(char *id_client, char *unsub_topic) {
  struct topic_t *topic_entry = ht_get(topics_ht, unsub_topic);

  if (!topic_entry) {
    return;
  }

  int index = 0;

  ll_node_t *current_subscriber = topic_entry->subscribers->head;

  while (current_subscriber) {
    if (strcmp(current_subscriber->data, id_client) == 0) {
      break;
    }

    index++;
    current_subscriber = current_subscriber->next;
  }

  if (!current_subscriber) {
    return;
  }

  /* Remove the client from the topic */
  ll_remove_nth_node(topic_entry->subscribers, index);


  struct subscriber_t *sub_entry = ht_get(subscribers_ht, id_client);
  send_ack(sub_entry->sockfd, ACK_UNSUBSCRIBE);
}

/* Add a new client subscription based on a topic and a SF factor */
void add_client_new_subscription(char *id_client, char *topic, int sf) {
  struct topic_status_t *new_topic_status = malloc(sizeof(struct topic_status_t));

  strcpy(new_topic_status->topic, topic);
  new_topic_status->status = sf;

  /* Check if the topic exists in the topics hashtable */
  if (!ht_has_key(topics_ht, topic)) {
    /* If not, add the new topic to the topics hashtable */
    struct topic_t *new_topic = malloc(sizeof(struct topic_t));
    strcpy(new_topic->topic, topic);
    new_topic->contents = ll_create(MSG_MAXSIZE);
    new_topic->subscribers = ll_create(ID_SIZE);

    ht_put(&topics_ht, topic, strlen(topic) + 1, new_topic, sizeof(struct topic_t));
  }

  struct topic_t *topic_entry = ht_get(topics_ht, topic);
  new_topic_status->last_msg = topic_entry->contents->size;

  /* Add the new_topic to the current client */
  struct subscriber_t *sub_entry = ht_get(subscribers_ht, id_client);
  ll_add_nth_node(sub_entry->topics, 0, new_topic_status);

  /* Mark that the current client is subscribed by adding 
   * him to the current topic
   * */
  ll_add_nth_node(topic_entry->subscribers, 0, id_client);
  send_ack(sub_entry->sockfd, ACK_SUBSCRIBE);
}

/* Add a new client to the subscribers hashtable with the specified TCP socket */
void add_new_client(char *id_client, int sockfd) {
  struct subscriber_t *new_sub = malloc(sizeof(struct subscriber_t));
  strcpy(new_sub->id, id_client);
  new_sub->sockfd = sockfd;
  new_sub->connected = 1;
  new_sub->topics = ll_create(sizeof(struct topic_status_t));

  ht_put(&subscribers_ht, id_client, strlen(id_client),
          new_sub, sizeof(struct subscriber_t));
}

/* Incrememnt with 1 the last message received by a subscriber on a topic */
void increment_last_msg(struct subscriber_t *subscriber, char *topic) {
  ll_node_t *current_topic = subscriber->topics->head;
  struct topic_status_t *current_topic_status;

  while (current_topic) {
    current_topic_status = current_topic->data;
    
    if (strcmp(topic, current_topic_status->topic) == 0) {
      (current_topic_status->last_msg)++;
    }

    current_topic = current_topic->next;
  }
}

/* Send all subscribers a new message corresponding to a certain topic */
void send_all_subscribers(linked_list_t *subscribers, char *topic, char *new_message) {
  ll_node_t *current = subscribers->head;
  char *id_client;
  struct subscriber_t *sub_entry;
  struct packet_t sent_packet;

  sent_packet.len = strlen(new_message);
  strcpy(sent_packet.message, new_message);

  while (current) {
    id_client = current->data;
    sub_entry = ht_get(subscribers_ht, id_client);

    /* If the subscriber is still connected send the message */
    if (sub_entry->connected) {
      send_all(sub_entry->sockfd, &sent_packet, sizeof(sent_packet));
    
      /* Increment the last message received */
      increment_last_msg(sub_entry, topic);
    }

    current = current->next;
  }
}

/* Update the topics hashtable with a new message for a certain topic */
void update_topics_ht(char *topic, char *new_message) {
  /* Check if the topics_ht already contains the current topic */
  if (!ht_has_key(topics_ht, topic)) {
    /* If not add a new topic */
    struct topic_t *new_topic = malloc(sizeof(struct topic_t));
    strcpy(new_topic->topic, topic);
    new_topic->contents = ll_create(MSG_MAXSIZE);
    new_topic->subscribers = ll_create(ID_SIZE + 1);

    ht_put(&topics_ht, topic, strlen(topic) + 1, new_topic, sizeof(struct topic_t));
  }

  struct topic_t *topic_entry = ht_get(topics_ht, topic);

  /* Add the new message to the contents of this topic, paying attention to the order */
  ll_add_nth_node(topic_entry->contents, topic_entry->contents->size, new_message);

  /* Send all subscribers a notification regarding the change */
  send_all_subscribers(topic_entry->subscribers, topic, new_message);
}

/* Send a broadcast message for all TCP clients */
void send_all_clients(struct pollfd *poll_fds, int num_clients, char *msg) {
	struct packet_t sent_packet;

	sent_packet.len = strlen(msg) + 1;
	strcpy(sent_packet.message, msg);

	for (int i = 3; i < num_clients; i++) {
		send_all(poll_fds[i].fd, &sent_packet, sizeof(sent_packet));
  }
}

void format_udp_msg(struct sockaddr_in *client_addr, char *msg, int bytes) {
  char buf[MSG_MAXSIZE];
  char *client_addr_buf = inet_ntoa(client_addr->sin_addr);
  uint16_t port = ntohs(client_addr->sin_port);

  char topic[TOPIC_SIZE + 1];
  topic[TOPIC_SIZE] = '\0';
  char type;
  
  int offset = 0;
  memcpy(topic, msg + offset, TOPIC_SIZE);
  offset += TOPIC_SIZE;

  memcpy(&type, msg + offset, 1);
  offset++;

  char sign;
  uint32_t value;
  uint16_t short_value;
  long long real_long_value;
  float real_short_value;
  char rest_msg[MAX_PAYLOAD_SIZE];

  /* Read the data accordinly to the type */
  switch (type) {
    case 0:      
      memcpy(&sign, msg + offset, 1);
      offset++;
      
      memcpy(&value, msg + offset, sizeof(uint32_t));
      offset += sizeof(uint32_t);

      real_long_value = (sign == 0 ? 1 : -1) * (long long) ntohl(value);

      sprintf(buf, "%s:%hu - %s - INT - %lld", client_addr_buf, port, topic, real_long_value);

      /* update the topics hashtable with the new message */
      update_topics_ht(topic, buf);
      break;
    case 1:   
      memcpy(&short_value, msg + offset, sizeof(uint16_t));
      offset += sizeof(uint16_t);

      real_short_value = ((float) ntohs(short_value)) / 100;

      sprintf(buf, "%s:%hu - %s - SHORT_REAL - %.2f", client_addr_buf, port, topic, real_short_value);
      
      /* update the topics hashtable with the new message */
      update_topics_ht(topic, buf);
      break;
    case 2:   
      memcpy(&sign, msg + offset, 1);
      offset++;
      
      memcpy(&value, msg + offset, sizeof(uint32_t));
      offset += sizeof(uint32_t);

      uint8_t power;
      memcpy(&power, msg + offset, sizeof(uint8_t));
      offset += sizeof(uint8_t);

      real_short_value = (sign == 0 ? 1 : -1) * (float) ntohl(value);

      while (power) {
        real_short_value /= 10;
        power--;
      }

      sprintf(buf, "%s:%hu - %s - FLOAT - %f", client_addr_buf, port, topic, real_short_value);

      /* update the topics hashtable with the new message */
      update_topics_ht(topic, buf);
      break;
    default:
      memcpy(rest_msg, msg + offset, bytes - offset);
      rest_msg[bytes - offset] = '\0';
      offset += (bytes - offset);
      
      sprintf(buf, "%s:%hu - %s - STRING - %s", client_addr_buf, port, topic, rest_msg);
      
      /* update the topics hashtable with the new message */
      update_topics_ht(topic, buf);
  }
}

void parse_client_request(int sockfd, int *num_clients, char *last_connect_ip, int last_connect_port) {
  struct packet_t received_packet;
  struct packet_t sent_packet;

  char id_client[ID_SIZE];
  char command[COMMAND_SIZE];
  char topic[TOPIC_SIZE + 1];

  char buf[MSG_MAXSIZE + 1];

  /* New messages from TCP clients */
  int rc = recv_all(sockfd, &received_packet, sizeof(received_packet));
  DIE(rc < 0, "recv");

  char recv_buf[MSG_MAXSIZE + 1];
  strcpy(recv_buf, received_packet.message);

  char *token = strtok(recv_buf, " \n");

  strcpy(id_client, token);

  token = strtok(NULL, " \n");
  
  strcpy(command, token);
  
  if (strncmp(command, EXIT, strlen(EXIT)) == 0) {
    printf("Client %s disconnected.\n", id_client);
    
    /* Mark the connected field for the subscriber as 0 */
    struct subscriber_t *sub = ht_get(subscribers_ht, id_client);

    sub->connected = 0;

    close(sockfd);
    return;
  }

  if (strncmp(command, NEW_CLIENT, strlen(NEW_CLIENT)) == 0) {            
    /* Check if the client was previously in the subscribers database */
    if (ht_has_key(subscribers_ht, id_client)) {
      struct subscriber_t *sub_entry = ht_get(subscribers_ht, id_client);
      
      /* If the client is still connect -- we got a duplicate */
      if (sub_entry->connected) {
        printf("Client %s already connected.\n", id_client);

        /* Refute the connection */
        strcpy(buf, DENIED);

        sent_packet.len = strlen(buf);
        strcpy(sent_packet.message, buf);

        rc = send_all(sockfd, &sent_packet, sizeof(sent_packet));

        close(sockfd);
        (*num_clients)--;
        return;
      } else {
        /* Client reconnected -- we don't have anything against this */
        printf("New client %s connected from %s:%hu.\n", id_client, last_connect_ip, last_connect_port);

        strcpy(buf, NON_DENIED);

        sent_packet.len = strlen(buf);
        strcpy(sent_packet.message, buf);

        rc = send_all(sockfd, &sent_packet, sizeof(sent_packet));

        sub_entry->connected = 1;
        sub_entry->sockfd = sockfd;

        (*num_clients)--;
        
        /* Update the messages while the client was disconnected */
        send_msg_reconnected(sub_entry);

        return;
      }
    }

    /* New client */
    printf("New client %s connected from %s:%hu.\n", id_client, last_connect_ip, last_connect_port);
    strcpy(buf, NON_DENIED);

    sent_packet.len = strlen(buf);
    strcpy(sent_packet.message, buf);
    add_new_client(id_client, sockfd);

    rc = send_all(sockfd, &sent_packet, sizeof(sent_packet));
    return;
  }
  
  if (strncmp(command, SUBSCRIBE, strlen(SUBSCRIBE)) == 0) {
    int sf;

    token = strtok(NULL, " \n");
    if (!token) {
      return;
    }

    strcpy(topic, token);

    token = strtok(NULL, " \n");
    if (!token) {
      return;
    }

    sf = token[0] - '0';
    if (sf != 0 && sf != 1) {
      return;
    }

    /* If the client was previously connected, simply update the fields */
    if (check_for_previous_subscription(id_client, topic, sf)) {
      return;
    }

    /* Client is newly connected to the topic */
    add_client_new_subscription(id_client, topic, sf);
    
    return;
  }
  
  if (strncmp(command, UNSUBSCRIBE, strlen(UNSUBSCRIBE)) == 0) {
    token = strtok(NULL, " \n");
    if (!token) {
      return;
    }

    strcpy(topic, token);

    /* Mark client as unsubscribed */
    remove_client_subscription(id_client, topic);
  }
}

void run_server(int sockfd[]) {
  char buf[MSG_MAXSIZE + 1];
  memset(buf, 0, MSG_MAXSIZE + 1);

  /* Info regarding the last connections */
  char *last_connect_ip;
  uint16_t last_connect_port;

  struct pollfd poll_fds[MAX_CONNECTIONS];
  int num_clients = 3;
  int rc;

  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  /* Set socket as listening socket for TCP */
  rc = listen(sockfd[1], MAX_CONNECTIONS);
  DIE(rc < 0, "listen");

  poll_fds[0].fd = STDIN_FILENO;
  poll_fds[0].events = POLLIN;
  
  poll_fds[1].fd = sockfd[0];
  poll_fds[1].events = POLLIN;

  poll_fds[2].fd = sockfd[1];
  poll_fds[2].events = POLLIN;

  while (1) {
    rc = poll(poll_fds, num_clients, -1);
    DIE(rc < 0, "poll");

    for (int i = 0; i < num_clients; i++) {      
      if (poll_fds[i].revents & POLLIN) {
        if (poll_fds[i].fd == STDIN_FILENO) {
          fgets(buf, sizeof(buf), stdin);
		  
          if (strncmp(buf, EXIT, strlen(EXIT)) == 0) {
            /* Close all the clients by sending the exit message */
            send_all_clients(poll_fds, num_clients, buf);
            return;
          }
        } else if (poll_fds[i].fd == sockfd[0]) {
          /* New datagram messages from UDP clients */
          char udp_message[MSG_MAXSIZE];

          struct sockaddr_in client_addr;
          socklen_t client_len = sizeof(client_addr);

          int rc = recvfrom(poll_fds[i].fd, udp_message, MSG_MAXSIZE, 0,
                            (struct sockaddr *)&client_addr, &client_len);

          DIE(rc < 0, "recvfrom");

          /* Call the format UDP message function to parse the input */
          format_udp_msg(&client_addr, udp_message, rc);
        } else if (poll_fds[i].fd == sockfd[1]) {
          /* New connections from TCP clients */
          int newsockfd = accept(poll_fds[i].fd, (struct sockaddr *)&client_addr, &client_len);
          DIE(newsockfd < 0, "accept");

          int enable = 1;
          /* Deactivate the Nagle Algorithm for the TCP listening socket */
          if (setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0)
            perror("setsockopt(TCP_NODELAY) failed");

          last_connect_ip = inet_ntoa(client_addr.sin_addr);
          last_connect_port = ntohs(client_addr.sin_port);

          poll_fds[num_clients].fd = newsockfd;
          poll_fds[num_clients].events = POLLIN;

          num_clients++;
        } else {
          parse_client_request(poll_fds[i].fd, &num_clients, last_connect_ip, last_connect_port);
        }
      }
    }
  }
}

int main(int argc, char *argv[]) {
  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  if (argc != 2) {
    printf("\n Usage: %s <port>\n", argv[0]);
    return 1;
  }

  /* Create hashtables for the subscribers and topics */
  subscribers_ht = ht_create(HMAX, hash_function_string, compare_function_strings);
  topics_ht = ht_create(HMAX, hash_function_string, compare_function_strings);

  /* Port parsed as number */
  uint16_t port;
  int rc = sscanf(argv[1], "%hu", &port);
  DIE(rc != 1, "Given port is invalid");

  int sockfd[2]; // sockfd[0] -- UPD, sockfd[1] -- TCP

  sockfd[0] = socket(AF_INET, SOCK_DGRAM, 0);
  DIE(sockfd[0] < 0, "socket");

  /* Fill the required fields for the UPD socket */ 
  struct sockaddr_in serv_addr;
  socklen_t socket_len = sizeof(struct sockaddr_in);

  int enable = 1;
  if (setsockopt(sockfd[0], SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    perror("setsockopt(SO_REUSEADDR) failed");

  memset(&serv_addr, 0, socket_len);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  rc = inet_pton(AF_INET, IP_SERVER, &serv_addr.sin_addr.s_addr);
  DIE(rc <= 0, "inet_pton");

  rc = bind(sockfd[0], (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
  DIE(rc < 0, "bind failed");

  /* Fill the required fields for the TCP socket -- listening socket */
  sockfd[1] = socket(AF_INET, SOCK_STREAM, 0);
  DIE(sockfd[1] < 0, "socket");

  rc = bind(sockfd[1], (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
  DIE(rc < 0, "bind");

  if (setsockopt(sockfd[1], SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    perror("setsockopt(SO_REUSEADDR) failed");


  /* Deactivate the Nagle Algorithm for the TCP listening socket */
  if (setsockopt(sockfd[1], IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0)
    perror("setsockopt(TCP_NODELAY) failed");

  run_server(sockfd);

  /* Close the 2 sockets */
  close(sockfd[1]);
  close(sockfd[0]);

  ht_free(subscribers_ht);
  ht_free(topics_ht);
  return 0;
}
