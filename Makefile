CFLAGS = -Wall -g

PORT = 12345

IP_SERVER = 127.0.0.1

all: server subscriber

server: server.c

client: subscriber.c

.PHONY: clean run_server run_client

run_server:
	./server ${PORT}

run_client:
	./subscriber ${IP_SERVER} ${PORT}

clean:
	rm -f server subscriber
