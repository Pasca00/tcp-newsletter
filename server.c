#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "helpers.h"

#define MAX_BUFFER_SIZE 1500

#define COMMAND_NEWSUB 1
#define COMMAND_SUBSCRIBE 2
#define COMMAND_UNSUBSCRIBE 3
#define COMMAND_EXIT 4

#define USER_IS_ONLINE 0
#define USER_EXISTS 1
#define USER_NOT_FOUND 2

#define STATUS_ONLINE 0
#define STATUS_OFFLINE 1

typedef struct topic {
	char title[50];
	int SF;
} Topic;

typedef struct client {
	char username[10];
	int ntopics;
	Topic topics[100];
	char isOnline;
	int sockid;
} Client;

typedef struct message {
	char topic[50];
	unsigned char data_type;
	char content[MAX_BUFFER_SIZE];
} Message;

typedef struct TCP_message {
	Message m;
	struct sockaddr_in source;
} TCP_message;

typedef struct Stored_messages {
	TCP_message *mTCP;
	int nmessages;
} Stored_messages;

Client* createClient() {
	Client* newClient = malloc(sizeof(Client));
	
	memset(newClient->username, 0, sizeof(newClient->username));
	memset(newClient->topics, 0, sizeof(newClient->topics));
	newClient->ntopics = 0;
	newClient->isOnline = 0;
	newClient->sockid = -1;
	
	return newClient;
}

Client** createClients(int nclients) {
	Client** clients = malloc(nclients * sizeof(Client*));
	
	for (int i = 0; i < nclients; i++) {
		clients[i] = createClient();
	}
	
	return clients;
}

Stored_messages *create_stored_messages() {
	Stored_messages *stored_messages = malloc(sizeof(Stored_messages));
	stored_messages->mTCP = malloc(50 * sizeof(TCP_message));
	stored_messages->nmessages = 0;
	
	return stored_messages;
}

char checkExistingUser(Client** clients, int nclients,char *username) {
	for (int i = 0; i < nclients; i++) {
		if (strcmp(clients[i]->username, username) == 0) {
			if (clients[i]->isOnline == STATUS_ONLINE) {
				return USER_IS_ONLINE;
			} else {
				return USER_EXISTS;
			}
		}
	}
	
	return USER_NOT_FOUND;
}

/**
 * Receives a command from a client and saves
 * the command type and any aditional info.
 *
 * @buffer message sent by client
 * @command pointer to an integer in which
 * 			the command type is saved
 * @op pointer to a set of any additional info
 *	   received (username, topic etc.)
 */
void getCommand(char* buffer, int *command,  char** op) {
	char *token = strtok(buffer, " \n");
	
	if (token == NULL) {
		*command = -1;
		return;
	}
	
	if (strcmp(token, "new") == 0) {
		*command = COMMAND_NEWSUB;
	} else if (strcmp(token, "subscribe") == 0) {
		*command = COMMAND_SUBSCRIBE;
	} else if (strcmp(token, "unsubscribe") == 0) {
		*command = COMMAND_UNSUBSCRIBE;
	} else if (strcmp(token, "exit") == 0) {
		*command = COMMAND_EXIT;
	}
	
	int i = 0;
	while ((token = strtok(NULL, " ")) != NULL) {
		strcpy(op[i], token);
		i++;
	}
}

void addClient(Client **clients, int *currClient, char *username, int sockid) {
	strcpy(clients[*currClient]->username, username);
	clients[*currClient]->sockid = sockid;
	clients[*currClient]->isOnline = STATUS_ONLINE;
	
	(*currClient)++;
}

Client *getClientBySock(Client **clients, int nclients, int sockid) {
	for (int i = 0; i < nclients; i++) {
		if (clients[i]->sockid == sockid) {
			return clients[i];
		}
	}
	
	return NULL;
}

/**
 * Searches for client by username. If client comes online
 * also updates its socket id.
 *
 * @clients list of clients
 * @nclients total number of clients in database
 * @username 
 * @status either STATUS_OFFLINE, or STATUS_ONLINE
 * @new_sockid
 */
void setClientStatus(Client **clients, int nclients, char *username, char status, int new_sockid) {
	for (int i = 0; i < nclients; i++) {
		if (strcmp(username, clients[i]->username) == 0) {
			if (status == STATUS_OFFLINE) {
				clients[i]->isOnline = status;
			} else {
				clients[i]->isOnline = status;
				clients[i]->sockid = new_sockid;
			}
			
			return;
		}
	}
}

void addTopic(Client *client, char *topic, char *SF) {
	strcpy(client->topics[client->ntopics].title, topic);
	client->topics[client->ntopics].SF = atoi(SF);
	client->ntopics++;
}

void subscribe(Client** clients, int nclients, int sockid, char *topic, char *SF) {
	Client *client = getClientBySock(clients, nclients, sockid);
	addTopic(client, topic, SF);
}

void unsubscribe(Client** clients, int nclients, int sockid, char *topic) {
	Client *client = getClientBySock(clients, nclients, sockid);
	for (int i = 0; i < client->ntopics; i++) {
		if (strcmp(client->topics[i].title, topic) == 0) {
			for (int j = i; j < client->ntopics - 1; j++) {
				client->topics[j] = client->topics[j + 1];
			}
			client->ntopics--;
			return;
		}
	}
}

void mySend(int sockfd, TCP_message mTCP) {
	uint32_t len = sizeof(TCP_message);
	len = htonl(len);
	send(sockfd, &len, sizeof(uint32_t), 0);
	send(sockfd, &mTCP, sizeof(TCP_message), 0);
}

void sendToClients(Client** clients, int nclients, TCP_message mTCP, Stored_messages *stored_messages) {
	char topic[50];
	strcpy(topic, mTCP.m.topic);
	for (int i = 0; i < nclients; i++) {
		for (int j = 0; j < clients[i]->ntopics; j++) {
			if (strcmp(clients[i]->topics[j].title, topic) == 0) {
				if (clients[i]->isOnline == STATUS_ONLINE) {
					mySend(clients[i]->sockid, mTCP);
				} else {
					if (clients[i]->topics[j].SF == 1) {
						stored_messages->mTCP[stored_messages->nmessages] = mTCP;
						stored_messages->nmessages++;
					}
				}
			}
		}
	}
}

void sendStoredMessages(Client **clients, int nclients, int sockid, Stored_messages *stored_messages) {
	for (int i = 0; i < nclients; i++) {
		if (clients[i]->sockid == sockid) {
			for (int j = 0; j < clients[i]->ntopics; j++) {
				if (clients[i]->topics[j].SF == 1) {
					for (int k = 0; k < stored_messages->nmessages; k++) {
						if (strcmp(clients[i]->topics[j].title, stored_messages->mTCP[k].m.topic) == 0) {
							mySend(clients[i]->sockid, stored_messages->mTCP[k]);
						}
					}
				}
			}
			
			return;
		}
	}
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	int nclients = 0;
	Client **clients = createClients(50);
	
	int UDP_socketfd = socket(AF_INET, SOCK_DGRAM, 0);
	int TCP_socketfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(UDP_socketfd < 0, "UDP Socket failed");
	DIE(TCP_socketfd < 0, "TCP Socket failed");
	
	fd_set readfds;
	fd_set temp_fds;
	FD_ZERO(&readfds);
	FD_ZERO(&temp_fds);
	
	struct sockaddr_in udp_address;
	memset(&udp_address, 0, sizeof(struct sockaddr_in));
	udp_address.sin_family = AF_INET;
	udp_address.sin_port = htons(atoi(argv[1]));
	udp_address.sin_addr.s_addr = htons(INADDR_ANY);
	memset(udp_address.sin_zero, 0, sizeof(struct in_addr));
	
	struct sockaddr_in tcp_address;
	memset(&tcp_address, 0, sizeof(struct sockaddr_in));
	tcp_address.sin_family = AF_INET;
	tcp_address.sin_port = htons(atoi(argv[1]));
	tcp_address.sin_addr.s_addr = htons(INADDR_ANY);
	memset(tcp_address.sin_zero, 0, sizeof(struct in_addr));
	
	int n = bind(UDP_socketfd, (struct sockaddr *) &udp_address, sizeof(struct sockaddr_in));
	DIE(n < 0, "UDP bind failed");
	
	n = bind(TCP_socketfd, (struct sockaddr *) &tcp_address, sizeof(struct sockaddr_in));
	DIE(n < 0, "TCP bind failed");
	
	n = listen(TCP_socketfd, 10);
	DIE(n < 0, "Error listening to server");
	
	int value = 1;
	setsockopt(TCP_socketfd, IPPROTO_TCP, TCP_NODELAY, (char*)&value, sizeof(int));
	
	
	FD_SET(UDP_socketfd, &readfds);
	FD_SET(TCP_socketfd, &readfds);
	FD_SET(STDIN_FILENO, &readfds);
	
	int nfds = TCP_socketfd;
	
	struct sockaddr_in new_client;
	unsigned int address_len = sizeof(struct sockaddr_in);
	
	Stored_messages *stored_messages = create_stored_messages();
	
	while (1) {
		temp_fds = readfds;
		int check = select(nfds + 1, &temp_fds, NULL, NULL, NULL);
		DIE(check < 0, "Select error");
		for (int i = 0; i <= nfds; i++) {
			if (FD_ISSET(i, &temp_fds)) {
				if (i == TCP_socketfd) {
					int new_socket = accept(i, (struct sockaddr *) &new_client, &address_len);
					if (new_socket == -1) {
						printf("Error accepting new connection\n");
					} else {
						FD_SET(new_socket, &readfds);
						if (new_socket > nfds) {
							nfds = new_socket;
						}
					}
				} else if (i == UDP_socketfd) {
					Message m;
					struct sockaddr_in source;
					unsigned int size = sizeof(source);
					int bytes = recvfrom(UDP_socketfd, &m, sizeof(Message), 0, (struct sockaddr*) &source, &size);
					if (bytes < 0) {
						printf("UDP client error\n");
					} else {
						TCP_message mTCP;
						mTCP.m = m;
						mTCP.source = source;
						sendToClients(clients, nclients, mTCP, stored_messages);
					}
				} else if (i == STDIN_FILENO) {
					char buffer[MAX_BUFFER_SIZE];
					memset(buffer, 0, MAX_BUFFER_SIZE);
					fgets(buffer, MAX_BUFFER_SIZE, stdin);
					
					if (strcmp(buffer, "exit\n") == 0) {
						TCP_message exit_message;
						strcpy(exit_message.m.topic, "exit");
						
						for (int i = 0; i <= nfds; i++) {
							if (FD_ISSET(i, &readfds) && i != UDP_socketfd && i != TCP_socketfd && i != STDIN_FILENO) {
								mySend(i, exit_message);
							}
						}
						
						close(TCP_socketfd);
						close(UDP_socketfd);
						return 0;
					}
				} else {
					uint32_t len;
					int bytes = recv(i, &len, sizeof(len), 0);
					len = ntohl(len);
					if (bytes < 0) {
						printf("Error receiving from client\n");
					} else {
						char buffer[1600];
						uint32_t currSum = 0;
						while (currSum < len) {
							bytes = recv(i, buffer + currSum, len - currSum, 0);
							currSum += bytes;
						}
					
						TCP_message *mTCP = (TCP_message*) buffer;
						int command;
						
						char **op = malloc(3 * sizeof(char*));
						for (int i = 0; i < 3; i++) {
							op[i] = malloc(50 * sizeof(char));
						}

						getCommand(mTCP->m.content, &command, op);
						switch (command) {
							case COMMAND_NEWSUB: ;
								char result = checkExistingUser(clients, nclients, op[0]);
								char addr_str[30];
								switch (result) {
									case USER_NOT_FOUND:
										addClient(clients, &nclients, op[0], i);
										inet_ntop(AF_INET, &(new_client.sin_addr.s_addr), addr_str, INET_ADDRSTRLEN);
										printf("New client %s connected from %s:%hu.\n", op[0], addr_str, ntohs(new_client.sin_port));
										break;
										
									case USER_EXISTS:
										setClientStatus(clients, nclients, op[0], STATUS_ONLINE, i);
										inet_ntop(AF_INET, &(new_client.sin_addr.s_addr), addr_str, INET_ADDRSTRLEN);
										printf("New client %s connected from %s:%hu.\n", op[0], addr_str, ntohs(new_client.sin_port));
										sendStoredMessages(clients, nclients, i, stored_messages);
										break;
									
									case USER_IS_ONLINE:
										printf("Client %s already connected.\n", op[0]);
										
										TCP_message exit_message;
										strcpy(exit_message.m.topic, "exit");
										
										mySend(i, exit_message);
										
										close(i);
										FD_CLR(i, &readfds);
										break;
								}
								
								break;
							
							case COMMAND_SUBSCRIBE:
								subscribe(clients, nclients, i, op[0], op[1]);
								break;
							
							case COMMAND_UNSUBSCRIBE:
								unsubscribe(clients, nclients, i, op[0]);
								break;
								
							case COMMAND_EXIT: ;
								Client *queryResult = getClientBySock(clients, nclients, i);
								if (queryResult == NULL) {
									fprintf(stderr, "User not found\n");
								} else {
									printf("Client %s disconnected.\n", queryResult->username);
									queryResult->isOnline = STATUS_OFFLINE;
									close(i);
									FD_CLR(i, &readfds);
								}
								break;
						}
						
						for (int i = 0; i < 3; i++) {
							free(op[i]);
						}
						free(op);
					}
				}
			}
		}
	}
	
	return 0;
}