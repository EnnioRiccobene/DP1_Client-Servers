/*
 * TEMPLATE 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include "../sockwrap.h"
#include "../errlib.h"

#define BUFLENGHT 255
#define OK_MSG "+OK"
#define MAX_S 1023
#define CHUNK 4096

char *prog_name;

int main (int argc, char *argv[])
{	
	int socket;
	char *addr, *port, *fname;
	char buf[BUFLENGHT];
	struct addrinfo *list;
	struct sockaddr_in destaddr;
	struct sockaddr_in *solvedaddr;

	prog_name = argv[0];

	if(argc < 4){
		printf("Errore! Scrivere come segue:\n%s <dest_host> <dest:port> <filename> [<filename>...] [-r]\n", prog_name);
		exit(1);
	}

	addr = argv[1];
	port = argv[2];

	Getaddrinfo(addr, port, NULL, &list);
	solvedaddr = (struct sockaddr_in *)list->ai_addr;

	/*Creazione della socket*/
	socket = Socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	/*Specifico indirizzo per il Bind*/
	memset(&destaddr, 0, sizeof(destaddr));
	destaddr.sin_family = AF_INET;
	destaddr.sin_port = solvedaddr->sin_port;
	destaddr.sin_addr.s_addr = solvedaddr->sin_addr.s_addr;

	printf("%s - Socket creata\n", prog_name);

	Connect(socket, (struct sockaddr *)&destaddr, sizeof(destaddr));
	printf("%s connesso a %s porta: %u\n", prog_name, inet_ntoa(destaddr.sin_addr), ntohs(destaddr.sin_port));

	/*finchè non finisco gli argomenti (filename) mando la get al server e aspetto, scarico
	il file e lo salvo in locale e faccio la printf dell'avvenuto trasferimento, al termine invio quit*/
	for(int i = 3; i < argc; i++){
		fname = argv[i];
		sprintf(buf, "GET %s\r\n", fname);
		Send(socket, buf, strlen(buf), MSG_NOSIGNAL);
		printf("%s - La GET è stata inviata\n", prog_name);

		fd_set rset;
		struct timeval tv;
		FD_ZERO(&rset);
		FD_SET(socket, &rset);
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		if(select(socket + 1, &rset, NULL, NULL, &tv) > 0){
			int n_read = 0; 
			char c;
			do {
				Recv(socket, &c, sizeof(char), 0);
				buf[n_read++] = c;
			} while((c != '\n') && (n_read < BUFLENGHT - 1));
			buf[n_read] = '\0';

			while((n_read > 0) && ((buf[n_read - 1] == '\r') || (buf[n_read - 1] == '\n'))){
				buf[n_read - 1] = '\0';
				n_read--;
			}

			if((n_read >= strlen(OK_MSG)) && (strncmp(buf, OK_MSG, strlen(OK_MSG))) == 0){
				char fnames[MAX_S + 1];
				sprintf(fnames, "%s", fname);
				
				unsigned int size = 0;
				unsigned int timestamp = 0;

				Readn(socket, &size, sizeof(size));
				Readn(socket, &timestamp, sizeof(timestamp));
				size = ntohl(size);
				timestamp = ntohl(timestamp);

				FILE *fp;
				if((fp = fopen(fnames, "wb")) != NULL){
					size_t remain_data = size;
					char buffer[CHUNK];
					size_t size_read = 0, written_data = 0;
					int a = 0;

					while(remain_data > 0){

						size_read = Recv(socket, buffer, sizeof(buffer), 0);
						
						if(size_read < 0){

							printf("%s - Errore nella recv()\n", prog_name);
							break;

						} else if(size_read == 0){

							a = -1;
							break;

						}

						written_data = fwrite(buffer, 1, size_read, fp);
						remain_data -= written_data;												

					}

					fclose(fp);

					if(a != 0){

						printf("%s - Errore: la connessione sul Server è stata chiusa\n", prog_name);
						exit(1); 

					}
					
					printf("%s - Il file ricevuto è stato scritto in %s\n", prog_name, fnames);

				} else {
					printf("Impossibile aprire il file '%s'\n", fnames);
				}

				printf("File ricevuto - Name: %s\n", fname);
				printf("File ricevuto - Size: %d\n", size);
				printf("File ricevuto - Timestamp: %d\n\n", timestamp);

				fname = NULL;

			} else {
				printf("Errore: ricevuta risposta '%s'\n", buf);
				exit(1);
			}
		} else {
			perror("Timeout, nessuna risposta ricevuta dal server\n");
		}			
	}
	sprintf(buf, "QUIT\r\n");
	Send(socket, buf, strlen(buf), MSG_NOSIGNAL);

	Close(socket);

	freeaddrinfo(list);

	return 0;
}
