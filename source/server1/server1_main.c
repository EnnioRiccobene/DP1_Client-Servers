/*
 * TEMPLATE 
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include "../errlib.h"
#include "../sockwrap.h"

#define BUFLENGHT 255
#define LISTEN_QUEUE 15
#define STR_MAX 1023
#define CHUNK 4096

#define ERR_MSG "-ERR\r\n"
#define OK_MSG "+OK\r\n"
#define QUIT_MSG "QUIT"
#define GET_MSG "GET"

char *prog_name;

int answer_to_request(int conn);

int main (int argc, char *argv[])
{
	int socket, conn;
	unsigned short porta;
	int opt_val;
	struct sockaddr_in client_addr, server_addr;
	socklen_t clientaddr_lenght = sizeof(client_addr);

	prog_name = argv[0];

	if(argc != 2){
		printf("Errore! Scrivere come segue:\n%s <port>\n", prog_name);
		exit(1);
	}

	porta = atoi(argv[1]);
	if(porta < 1024){
		printf("Permesso negato. Scegliere una porta maggiore di 1025\n");
		exit(1);
	}

	/*Creazione della socket*/	
	socket = Socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	opt_val = 1;
	if(setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) < 0){
		printf("setsockopt(SO_REUSEADDR) fallita\n");
	}


	/*Specifico indirizzo per il Bind*/
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(porta); //htons converte unsigned short integer da host byte order a network byte order
	server_addr.sin_addr.s_addr = htonl (INADDR_ANY);

	Bind(socket, (SA*) &server_addr, sizeof(server_addr));

	printf("%s - Socket creata. Server in ascolto all'indirizzo %s sulla porta %u\n", prog_name, inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port)); 

	Listen(socket, LISTEN_QUEUE);

	while(1){
		//da qui in poi non uso le funzioni della libreria sockwrap perchè il fallimento di una connessione con un client non può terminare l'esecuzione di tutto il server
		conn = accept(socket, (SA*) &client_addr, &clientaddr_lenght); 
		if(conn < 0){
			printf("%s - Errore: accept() fallita\n", prog_name);
		} else{
			if(answer_to_request(conn) != 0){
				printf("%s - Errore nel servire il Client\n", prog_name);
			}
			
			if (close(conn) != 0) {
				printf("%s - Errore: close() fallita\n", prog_name);
			} else {
				printf("%s - Connessione chiusa\n\n", prog_name);
			}			
		}		
	}

	return 0;
}

int answer_to_request(int conn){
	char buf[BUFLENGHT + 1]; //per tenere conto di \0
	int return_val = 0;

	while(1){
		int n_read = 0;
		char c;

		do{
			int n = recv(conn, &c, sizeof(char), 0); 

			if(n == 1){
				buf[n_read++] = c;
			} else if(n < 0){
				printf("%s - Errore: recv() fallita\n", prog_name);
				break;
			} else {
				break;
			}
		} while((c != '\n') && (n_read < BUFLENGHT - 1));

		if(n_read == 0){
			return 0;
		}

		/*Metto il terminatore di stringa alla fine*/
		buf[n_read] = '\0';

		while((n_read > 0) && ((buf[n_read - 1] == '\r') || (buf[n_read - 1] == '\n'))){
			buf[n_read - 1] = '\0';
			n_read--;
		}
		printf("%s - Ricevuta la stringa '%s'\n", prog_name, buf);

		/*Comando GET*/
		if((n_read > strlen(GET_MSG)) && (strncmp(buf, GET_MSG, strlen(GET_MSG)) == 0)){
			char filename[STR_MAX + 1];
			strcpy(filename, buf + 4);
			printf("%s - Il Client ha chiesto il file '%s'\n", prog_name, filename);
	
			struct stat info;
			int res = stat(filename, &info);//prendo le info dal file

			if(res == 0){

				FILE *fp;

				if((fp = fopen(filename, "rb")) != NULL){

					unsigned int size = info.st_size;
					unsigned int timestamp = info.st_mtime;

					if(send(conn, OK_MSG, strlen(OK_MSG), MSG_NOSIGNAL) != strlen(OK_MSG)){
						
						res = -1;

					} else{

						printf("%s - Messaggio +OK mandato al Client\n",prog_name);

						uint32_t val_size = htonl(size);
						
						if(send(conn, &val_size, sizeof(val_size), MSG_NOSIGNAL) != sizeof(val_size)){
							
							res = -1;
						}

						uint32_t val_timestamp = htonl(timestamp);
						
						if(send(conn, &val_timestamp, sizeof(val_timestamp), MSG_NOSIGNAL) != sizeof(val_timestamp)){
							
							res = -1;
						}

						char buffer[CHUNK];
						size_t remain_data = size;
						size_t size_read = 0, sent_data = 0;
						
						while(remain_data > 0){
							
							size_read = fread(buffer, 1, sizeof(buffer), fp);

							if((sent_data = send(conn, buffer, size_read, MSG_NOSIGNAL)) != size_read){
			
								res = -1;
								break;

							} else {
								remain_data -= sent_data;
							}							
						}

						printf("%s - File '%s' inviato al Client\n\n", prog_name, filename);						
						
					}
					
					fclose(fp);

				} else {
					res = -1;
				}
			}

			if(res != 0){

				if(send(conn, ERR_MSG, strlen(ERR_MSG), MSG_NOSIGNAL) != strlen(ERR_MSG)){
					printf("%s - Errore: send() fallita o il Client ha chiuso la connessione\n", prog_name);
				}
			}

		} else if((n_read >= strlen(QUIT_MSG)) && (strncmp(buf, QUIT_MSG, strlen(QUIT_MSG)) == 0)){

			printf("%s - Il Client ha richiesto di terminare la connessione\n", prog_name);
			break;

		} else {

			if(send(conn, ERR_MSG, strlen(ERR_MSG), MSG_NOSIGNAL) != strlen(ERR_MSG)){

				printf("%s - Errore: send() fallita o il Client ha chiuso la connessione\n", prog_name);
				return_val = 1;
				break;

			}			
		}
	}
	
	return return_val;

}
