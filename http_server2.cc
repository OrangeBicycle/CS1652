#include "minet_socket.h"
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>

#define BUFSIZE 1024
#define FILENAMESIZE 100

int handle_connection(int sock);

int main(int argc, char * argv[]) {

	struct node {
		int socket_descriptor;
		bool server;
		struct node *prev;
		struct node *next;
	};
	
	struct node *server_socket_head = (node *)malloc(sizeof(struct node));
	struct node *current_node;
	struct node *tail;
	struct node *temp;
	fd_set read_list;
	int readable = -1;

    int server_port         = -1;
    int rc                  =  0;
    int server_socket       = -1;
    int connection_socket   = -1;
    minet_socket_types mode;
    struct sockaddr_in saddr;	

    /* parse command line args */
    if (argc != 3) {
	fprintf(stderr, "usage: http_server1 k|u port\n");
	exit(-1);
    }
		
    if(strcmp(argv[1], "k") == 0)
        mode = MINET_KERNEL;
    else if(strcmp(argv[1], "u") == 0)
        mode = MINET_USER;
    else {
        fprintf(stderr, "usage: http_server1 k|u port\n");
	    exit(-1);
    }	

    server_port = atoi(argv[2]);

    if (server_port < 1500) {
	fprintf(stderr, "INVALID PORT NUMBER: %d; can't be < 1500\n", server_port);
	exit(-1);
    }
    
    /* initialize and make socket */
	if(minet_init(mode) < 0) {
        fprintf(stderr, "MINET INITIALIZATION FAILED\n");
        exit(-1);
    }
	
    if((server_socket = minet_socket(SOCK_STREAM)) < 0) {
        fprintf(stderr, "SOCKET CREATION FAILED\n");
        exit(-1);
    } 

    /* set server address*/
	memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(server_port);  

    /* bind listening socket */
	if(minet_bind(server_socket, (struct sockaddr_in*)&saddr) < 0) {
        fprintf(stderr, "FAILED TO BIND SOCKET TO ADDRESS\n");
        exit(-1);
    }

    /* start listening */
	if(minet_listen(server_socket, 50) < 0) {
        fprintf(stderr, "FAILED TO LISTEN ON SOCKET\n");
        exit(-1);
    }

    /* connection handling loop: wait to accept connection */
	//while(connection_socket = minet_accept(server_socket, (struct sockaddr_in*)&saddr) >= 0) {        
    //}
	server_socket_head -> socket_descriptor = server_socket;
	server_socket_head -> server = true;
	server_socket_head -> next = NULL;
	server_socket_head -> prev = NULL;
	tail = server_socket_head;

    while(true) {
	
		/* create read list */
		current_node = server_socket_head;
		FD_ZERO(&read_list);
		while(current_node != NULL) {
			FD_SET(current_node -> socket_descriptor, &read_list);
			current_node = current_node -> next;
		}
	
		/* do a select */
		readable = minet_select(FD_SETSIZE, &read_list, NULL, NULL, NULL);
		if(readable < 0) {
			fprintf(stderr, "UNEXPECTED NUMBER OF READABLE SOCKETS\n");
			exit(1);
		} else if(readable == 0) {
			continue;
		}
	
		/* process sockets that are ready */
	
		/* for the accept socket, add accepted connection to connections */
		if(FD_ISSET(server_socket_head -> socket_descriptor, &read_list)) {
			connection_socket = minet_accept(server_socket, (struct sockaddr_in*)&saddr);
			tail -> next = (struct node*)malloc(sizeof(struct node));
			(tail -> next) -> prev = tail;
			tail = tail -> next;
			tail -> socket_descriptor = connection_socket;
			tail -> server = false;
			tail -> next = NULL;
		}
	
		/* for a connection socket, handle the connection */
		current_node = server_socket_head -> next;
		while(current_node != NULL) {
			if(FD_ISSET(current_node -> socket_descriptor, &read_list)) {	
				rc = handle_connection(current_node -> socket_descriptor);
				if(rc < 0) {
					fprintf(stderr, "RECEIVED A 404 HTTP STATUS CODE (FILE NOT FOUND)\n");
				}
				(current_node -> prev) -> next = current_node -> next;
				(current_node -> next) -> prev = current_node -> prev;
				temp = current_node -> prev;
				free(current_node);
				current_node = temp;
			}
			current_node = current_node -> next;
		}
	
    }
	
	free(server_socket_head);
	minet_close(server_socket);
	minet_deinit();
	
}

int handle_connection(int sock) {
    bool ok = false;
	
    char * ok_response_f = "HTTP/1.0 200 OK\r\n"	\
	"Content-type: text/plain\r\n"			\
	"Content-length: %d \r\n\r\n";
	
    char * notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"	\
	"Content-type: text/html\r\n\r\n"			\
	"<html><body bgColor=black text=white>\n"		\
	"<h2>404 FILE NOT FOUND</h2>\n"
	"</body></html>\n";
	
    size_t buflen = 256;
    char *buf = (char *)malloc(buflen);
    unsigned int index = 0;
    bool cr = false;
    bool crlf = false;
    bool crlfcr = false;
	
    char *req_file;
    FILE *fp;
    long filesize;
	
    char *ok_response;
    int headlen;
    
    /* first read loop -- get request and headers*/
    while(true) {
        // check whether writing to the current location would overflow the buffer:
        // if so, allocate more space for the buffer
        if(index == buflen) {
            buflen = buflen * 2;
            buf = (char *)realloc(buf, buflen);
        }
		
        // read a byte in from the socket and write to the buffer
        if(minet_read(sock, &buf[index], 1) != 1) {
            fprintf(stderr, "UNEXPECTED NUMBER OF BYTES READ FROM SOCKET\n");
            exit(1);
        }
		
        // check for end of request
        if(buf[index] == '\r') {
            if(crlf == true) {
                crlfcr = true;
                cr = crlf = false;
            } else {
                cr = true;
                crlf = crlfcr = false;
            }
        }
        else if(buf[index] == '\n') {
            if(crlfcr == true) {
                break;
            } else if(cr == true) {
                crlf = true;
                cr = crlfcr = false;
            } else {
                cr = crlf = crlfcr = false;
            }
        }
        index++;
    } // end of read loop
    
    /* parse request to get file name */
    /* Assumption: this is a GET request and filename contains no spaces*/
    if(strcmp(strtok(buf, " "), "GET") != 0) {
        fprintf(stderr, "EXPECTED A \"GET\" REQUEST");
        exit(1);
    }
    req_file = strtok(NULL, " ");
	
    /* try opening the file */
    if((fp = fopen(req_file, "r")) != NULL) {
        ok = true;
        fseek(fp, 0, SEEK_END);
        filesize = ftell(fp);
        rewind(fp);
    }
	
    /* send response */
    if (ok) {
	    /* send headers */
        if((headlen = sprintf(ok_response, ok_response_f, filesize)) <= 0) {
			fprintf(stderr, "FOUND UNEXPECTED NUMBER OF BYTES IN HEADER\n");
            exit(1);
		}
        if(minet_write(sock, ok_response, headlen) != headlen) {
            fprintf(stderr, "WROTE UNEXPECTED NUMBER OF BYTES TO SOCKET\n");
            exit(1);
        }
	    /* send file */
        //buf = (char *)realloc(buf, 1);
		while(!feof(fp)) {
			fread(buf, 1, 1, fp);
			if(minet_write(sock, buf, 1) != 1) {
				fprintf(stderr, "WROTE UNEXPECTED NUMBER OF BYTES TO SOCKET\n");
				exit(1);
			}
		}
		
    } else {
	    // send error response
        if(minet_write(sock, notok_response, sizeof(notok_response)) != sizeof(notok_response)) {
            fprintf(stderr, "WROTE UNEXPECTED NUMBER OF BYTES TO SOCKET\n");
            exit(1);
        }
    }
    
    /* close socket and free space */
    minet_close(sock);
    free(buf);
	
    if (ok) {
	    return 0;
    } else {
	    return -1;
    }
}
