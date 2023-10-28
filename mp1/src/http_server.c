//3180300806
/*
** server.c -- a stream socket server demo
*/
// Import necessary libraries
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "3490"  // the port users will be connecting to
#define SIZE 1024

#define BACKLOG 10	 // how many pending connections queue will hold

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	//If arguments not present
	if (argc != 2) {
	    fprintf(stderr, "Please specify port number:\n");
	    exit(1);
	}
	//Create a file pointer
	FILE *fptr = NULL;
	int sockfd, new_fd;  	// Server socket and new connection socket
	struct addrinfo hints, *servinfo, *p;	// Information about the server and client
	struct sockaddr_storage their_addr; 	// Client's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;								// For socket options
	char s[INET6_ADDRSTRLEN];				// To store the client's IP address
	char req_header[SIZE];					//Buffer for http header request
	char file_content[SIZE];				//Buffer for file content
	char *file_name = NULL;					//Pointer to requested file name
	int rv, cnt;
	
	//Initialize address info structure
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	//Get address info of the server
	if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
	// Loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	//Start listening for incoming connections
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		// their_addr has the client's IP addr
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		// enable concurrency
		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			if (recv(new_fd, req_header, SIZE-1, 0) == -1)
				perror("recv");

			// Check for a bad request (no "GET" in the header)
			if (strlen(req_header) < 4 || strncmp(req_header, "GET ", 4) != 0) {
				if (send(new_fd, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0) == -1)
						perror("send");
			} else {
				if ((req_header[4]) != '/') {
					if (send(new_fd, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0) == -1)
						perror("send");
				} else {
					// Check for a bad request (no '/' in the request)
					file_name = &req_header[4];
					for (cnt = 0; file_name[cnt] != '\0'; cnt++) {
						if (file_name[cnt] == ' ' || file_name[cnt] == '\n')  break;
						if (file_name[cnt] == '\r')  break;
					}
					file_name += cnt;
					// Check for a bad request if "HTTP/1.1" is not present
					if (strlen(file_name) < 9 || strncmp(file_name, " HTTP/1.1", 9) != 0) {
						if (send(new_fd, "HTTP/1.1 400 Bad Request\r\n\r\n", 28, 0) == -1) 
							perror("send");
						file_name = NULL;
					} else {
						file_name = &req_header[4];
						file_name[cnt] = '\0';
					}
				}
				
			}
			if (file_name == NULL);
			 // Handle the case where the file is not present
			else if ((fptr = fopen(file_name+1, "r")) == NULL) {
				if (send(new_fd, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0) == -1)
					perror("send");
			} else {
				 // File is successfully found
				memset(file_content, 0, sizeof(file_content));
				memcpy(file_content, "HTTP/1.1 200 OK\r\n\r\n", 19);
				fread(file_content+19, sizeof(char), SIZE-20, fptr);
				do {
					if (send(new_fd, file_content, strlen(file_content), 0) == -1)
						perror("send");
					memset(file_content, 0, sizeof(file_content));
				} while (fread(file_content, sizeof(char), SIZE-1, fptr) > 0);
			}
			fclose(fptr);
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}