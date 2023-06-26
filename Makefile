build: server subscriber
CFLAGS = -Wall -Wextra -g -Wno-error=unused-variable
CINCLUDES = -I include/
all: server subscriber

hashtable.o: lib/hashtable.c include/hashtable.h include/utils.h

common.o: lib/common.c include/common.h include/hashtable.h

server: server.c include/utils.h lib/hashtable.o lib/common.o
	gcc  $(CFLAGS) $(CINCLUDES) server.c hashtable.o common.o -o server

subscriber: subscriber.c include/utils.h lib/common.o
	gcc  $(CFLAGS) $(CINCLUDES) subscriber.c common.o -o subscriber

.c.o:
	gcc $(CINCLUDES) $(CFLAGS) -g -c $?


.PHONY: clean run_subscriber run_server

run_server:
	./server

run_subscriber:
	./subscriber

clean:
	rm -f server subscriber *.o include/*.h.gch
