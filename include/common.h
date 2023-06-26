#ifndef __COMMON_H__
#define __COMMON_H__

#include <stddef.h>
#include <stdint.h>

#include "hashtable.h"

#define MSG_MAXSIZE 2048
#define SUBSCRIBER_MSG_MAXSIZE 1024
#define TOPIC_SIZE 50
#define ID_SIZE 128
#define COMMAND_SIZE 128

#define EXIT "exit"
#define DENIED "denied"
#define NON_DENIED "non_denied"
#define SUBSCRIBE "subscribe"
#define UNSUBSCRIBE "unsubscribe"
#define NEW_CLIENT "new_client"
#define ACK_SUBSCRIBE "ack_subscribe"
#define ACK_UNSUBSCRIBE "ack_unsubscribe"

int send_all(int sockfd, void *buff, size_t len);
int recv_all(int sockfd, void *buff, size_t len);

struct packet_t {
  uint16_t len;
  char message[MSG_MAXSIZE];
};

struct topic_t {
  char topic[TOPIC_SIZE + 1]; /* topic name */
  linked_list_t *contents; /* messages previously sent for that topic */
  linked_list_t *subscribers; /* current subscribers to this topic */
};

struct subscriber_t {
  char id[ID_SIZE + 1]; /* ID of the subscriber */
  int sockfd; /* associated fd on the server interface */
  int connected; /* current status */
  linked_list_t *topics; /* subscribed topics */
};

struct topic_status_t {
  char topic[TOPIC_SIZE + 1]; /* topic name */
  int status; /* SF field */
  int last_msg; /* last message sent for a specific client on the topic */
};

#endif
