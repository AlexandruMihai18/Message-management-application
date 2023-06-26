Alexandru Mihai, 323CA, alexandurmihai200913@gmail.com

# Client - Server application for message management

## Brief description

I started from the code from the TCP and UDP labs and work my way
through in order to make a stable server-client application.

In addition, the hashtable and linked list implementations
are taken from my DSA 1st year course. 

I have implemented all the required features and assured a stand alone
application, paying attention to any kind of wrong inputs or misleading
user commands.

## Project Structure

* **server.c** - server implementation
* **client.c** - client implementation
* **include** - headers for utility, auxiliary structures and common sockets
functions
* **lib** - folder including header functions implementation

## Application flow

* The server can be started on a given port by using the following command:

```
./server <PORT_SERVER>
```

* The client can connect to the server with the following command:

```
./subscriber <ID_CLIENT> <IP_SERVER> <PORT_SERVER>
```

* The clients can send subscription / unsubscription requests using the
followinng commands, (SF is 0 or 1):

```
subscribe <TOPIC> <SF>
unsubscribe <TOPIC>
```

* The server will receive topic specific messages from UDP clients and forward
them according to the subscribers requests.

**NOTE:** Any other provided input will not receive any kind of response,
being considered wrong or mispelled by the server (however the server
will continue to function regularly).

## Implementation details

The server works on 3 main sockets: STDIN socket, Datagram socket
(for UDP) - which receives the required datagrams from the UDP clients
and a Stream socket (for TCP) - the listening socket for the incomming
subscriber connections. When a new client tries to connect to the server
an additional TCP socket is created and servers a single connection.

When a new subscriber tries to connect to the server, it will first send
a NEW_CLIENT message, announcing it's intention to connect.
If another client exists currently on the server with the same
ID_CLIENT, it's connection will be refuted and it will close instantly.

The messages from the subscribers start with the ID_CLIENT
in order to easily identify each client in the database.
After a subscriber send an intention to subscribe / unsubscribe to a topic,
it will receive from the server a ACK message, that annouce the success
state of the request. If the ACK message is missing the subscription failed.

The UDP clients are treated individually, receiving and parsing each possible
input.

For the databases, I have decided that the **SUBSCRIBERS** will be stored
in a hashtable containing the name, respective socket, connection status
and a list of topic status structures. The **TOPIC STATUS** structures consists
of the name of the topic, type of forwarding (SF factor) and
the last message send, used for future reconnection or resubscription
to the topic. The **TOPICS** and their respective messages are stored
in another hashtable used for linking all the passed messages from the UDP
clients (in the respective order) and the connected clients. When a
subscriber chooses to unsubscribe, he is simply eliminated from
the list of clients that have access to the topic.

Searching a SUBSCRIBER / TOPIC in the database is done in O(1), considering 
that the hashtable has a resizing function used in order to increase the 
efficiency of searching. The connection between either subscriber - topic and
topic - subscriber can be summarized as having a O(N) time complexity relative
to the size of the lists, considering that we iterate through each element and
check if it corresponds to the required candidate.

