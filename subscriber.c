#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "helpers.h"

#define MAX_BUFFER_SIZE 1500

#define COMMAND_EXIT 0
#define COMMAND_SUBSCRIBE 1
#define COMMAND_UNSUBSCRIBE 2

#define TYPE_INT 0
#define TYPE_SHORT 1
#define TYPE_FLOAT 2
#define TYPE_STRING 3

typedef struct message {
	char topic[50];
	unsigned char data_type;
	char content[MAX_BUFFER_SIZE];
} Message;

typedef struct TCP_message {
	Message m;
	struct sockaddr_in source;
} TCP_message;

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
	if (strncmp(buffer, "exit", 4) == 0) {
		*command = COMMAND_EXIT;
		return;
	}
	
	char *token = strtok(buffer, " \n");
	
	if (token == NULL) {
		*command = -1;
		return;
	}
	
	if (strcmp(token, "subscribe") == 0) {
		*command = COMMAND_SUBSCRIBE;
	} else if (strcmp(token, "unsubscribe") == 0) {
		*command = COMMAND_UNSUBSCRIBE;
	}
	
	int i = 0;
	while ((token = strtok(NULL, " ")) != NULL) {
		strcpy(op[i], token);
		i++;
	}
}

int main(int argc, char** argv) {
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	int nfds = sockfd;
	
	char username[10];
	strcpy(username, argv[1]);
	
	struct sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(atoi(argv[3]));
	int n = inet_pton(AF_INET, argv[2], &server_address.sin_addr);
	DIE(n < 0, "Address not supported.");
	
	int value = 1;
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&value, sizeof(int));
	
	n = connect(sockfd, (struct sockaddr*) &server_address, sizeof(server_address));
	DIE(n < 0, "Error connecting to server\n");
	
	TCP_message connectMessage;
	
	sprintf(connectMessage.m.content, "new %s", username);
	uint32_t connectLen = sizeof(TCP_message);
	connectLen = htonl(connectLen);
	
	n = send(sockfd, &connectLen, sizeof(uint32_t), 0);
	n = send(sockfd, &connectMessage, sizeof(TCP_message), 0);
	
	fd_set readfds;
	fd_set temp_fds;

	FD_ZERO(&temp_fds);
	FD_ZERO(&readfds);
	
	FD_SET(sockfd, &readfds);
	FD_SET(STDIN_FILENO, &readfds);
	
	while(1) {
		temp_fds = readfds;
		int check = select(nfds + 1, &temp_fds, NULL, NULL, NULL);
		DIE(check < 0, "Error on select");
		
		for (int i = 0; i <= nfds; i++) {
			if (FD_ISSET(i, &temp_fds)) {
				if (i == STDIN_FILENO) {
					char buffer[MAX_BUFFER_SIZE];
					memset(buffer, 0, MAX_BUFFER_SIZE);
					fgets(buffer, MAX_BUFFER_SIZE, stdin);
					int command = -1;
					char **op = (char**)malloc(3 * sizeof(char*));
					for (int i = 0; i < 3; i++) {
						op[i] = (char*)malloc(50 * sizeof(char));
					}
					
					getCommand(buffer, &command, op);
					int check;
					
					TCP_message mTCP;
					uint32_t len = sizeof(TCP_message);
					len = htonl(len);
					
					switch (command) {
						case COMMAND_EXIT:
							FD_CLR(i, &readfds);
							FD_CLR(i, &temp_fds);
							FD_CLR(sockfd, &readfds);
							FD_CLR(sockfd, &temp_fds);
							strcpy(mTCP.m.content, buffer);
							
							send(sockfd, &len, sizeof(uint32_t), 0);
							send(sockfd, &mTCP, sizeof(TCP_message), 0);
							
							close(sockfd);
							return 0;
						
						case COMMAND_SUBSCRIBE:
							sprintf(mTCP.m.content, "subscribe %s %s", op[0], op[1]);
							check = send(sockfd, &len, sizeof(uint32_t), 0);
							check = send(sockfd, &mTCP, sizeof(TCP_message), 0);
							if (check < 0) {
								printf("Error sending to server\n");
							} else {
								printf("Subscribed to topic.\n");
							}
							break;
						
						case COMMAND_UNSUBSCRIBE:
							sprintf(mTCP.m.content, "unsubscribe %s", op[0]);
							check = send(sockfd, &len, sizeof(uint32_t), 0);
							check = send(sockfd, &mTCP, sizeof(TCP_message), 0);
							if (check < 0) {
								printf("Error sending to server\n");
							} else {
								printf("Unsubscribed from topic.\n");
							}
							break;
					}
					
					for (int i = 0; i < 3; i++) {
						free(op[i]);
					}
					free(op);
				} else {
					int n;
					uint32_t len;
					n = recv(i, &len, sizeof(uint32_t), 0);
					len = ntohl(len);
					if (n < 0) {
						FD_CLR(i, &readfds);
						FD_CLR(i, &temp_fds);
						FD_CLR(sockfd, &readfds);
						FD_CLR(sockfd, &temp_fds);
						close(sockfd);
						return 0;
					}
					
					char buffer[1600];
					uint32_t currSum = 0;
					while (currSum < len) {
						n = recv(i, buffer + currSum, len - currSum, 0);
						if (n < 0) {
							FD_CLR(i, &readfds);
							FD_CLR(i, &temp_fds);
							FD_CLR(sockfd, &readfds);
							FD_CLR(sockfd, &temp_fds);
							close(sockfd);
							return 0;
						}
						currSum += n;
					}
					
					TCP_message *mTCP = (TCP_message*) buffer;
					if (strcmp(mTCP->m.topic, "exit") == 0) {
						FD_CLR(i, &readfds);
						FD_CLR(i, &temp_fds);
						FD_CLR(sockfd, &readfds);
						FD_CLR(sockfd, &temp_fds);
						close(sockfd);
						return 0;
					}
						
					char saddr[30];
					inet_ntop(AF_INET, &(mTCP->source.sin_addr), saddr, INET_ADDRSTRLEN);
					switch (mTCP->m.data_type) {
						case TYPE_INT: ;
							char sign = mTCP->m.content[0];
							uint32_t *value_int = (uint32_t *) &(mTCP->m.content[1]);
							
							if (sign == 0) {
								printf("%s:%hu - %s - INT - %d\n", saddr, mTCP->source.sin_port, mTCP->m.topic, ntohl(*value_int));
							} else {
								printf("%s:%hu - %s - INT - %d\n", saddr, mTCP->source.sin_port, mTCP->m.topic, (-1) * ntohl(*value_int));
							}
							
							break;
							
						case TYPE_SHORT: ;
							uint16_t *value_short = (uint16_t *) mTCP->m.content;
							printf("%s:%hu - %s - SHORT_REAL - %.2f\n", saddr, mTCP->source.sin_port, mTCP->m.topic, ntohs(*value_short) / 100.0);
							break;
							
						case TYPE_FLOAT: ;
							char sign_float = mTCP->m.content[0];
							uint32_t *value_float = (uint32_t *) &(mTCP->m.content[1]);
							uint8_t *power = (uint8_t *) &(mTCP->m.content[5]);
							
							double div = 1;
							for (int i = 0; i < *power; i++) {
								div *= 10;
							}
								
							if (sign_float == 0) {
								printf("%s:%hu - %s - FLOAT - %.4f\n", saddr, mTCP->source.sin_port, mTCP->m.topic, ntohl(*value_float) / div);
							} else {
								printf("%s:%hu - %s - FLOAT - -%.4f\n", saddr, mTCP->source.sin_port, mTCP->m.topic, ntohl(*value_float) / div);
							}
							
							break;
							
						case TYPE_STRING:
							printf("%s:%hu - %s - STRING - %s\n", saddr, mTCP->source.sin_port, mTCP->m.topic, mTCP->m.content);
							break;
					}
					
				}
			}
		}
	}
	
	return 0;
}