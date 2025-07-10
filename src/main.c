#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
int main() {
    setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
	//
	server_fd = socket(AF_INET, SOCK_STREAM, 0);  
	// socket((int)domain, (int)type, (int)protocol) AF_INET: IPv4, AF_INET6: IPv6, SOCK_STREAM: TCP, SOCK_DGRAM(UDP
	
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	//
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	//
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	//
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	//
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	//
	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);
	//
	while (1) {
		int client = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
		if (client == -1) continue;
		printf("Client connected\n");
		//
	    
		pid_t pid = fork();
		pid_t main_pid = getpid();
		
		if (pid != 0) {
			close(client);
			continue;
		}

		char* buf = (char*) malloc(512);
		
		int num_bytes_recv = recv(client, buf, 512 - 1, 0);
		if (num_bytes_recv <= 0) {
			printf("Received message failed: %s \n", strerror(errno));
			return 1;
		}
		buf[num_bytes_recv] = '\0';
		
		char response[4096];

		if (strstr(buf, "GET /echo/") != NULL) {
			char* sub_str = strstr(buf, "/echo/");
			sub_str += strlen("/echo/");
			char* end = strchr(sub_str, ' ');
			size_t len = 0;
			if (end != NULL) {
				len = end - sub_str;
			}
			else {
				len = strlen(sub_str);
			}

			char* echo_str = (char*) malloc(len + 1);
			strncpy(echo_str, sub_str, len);

			int response_length	= snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(echo_str), echo_str);
			free(echo_str);
		}
		
		else if (strstr(buf, "GET /user-agent ") != NULL) {
			char* parts= strtok(buf, "\r\n");
			while(strstr(parts, "User-Agent: ") == NULL) {
				parts = strtok(NULL, "\r\n"); 
			}
			char* echo_str = strchr(parts, ' ');
			echo_str += strlen(" ");
			int response_length = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(echo_str), echo_str);
		}
		else if (strstr(buf, "GET /files/") != NULL) {
			char* parts = strtok(buf, " ");
			while(strstr(parts, "/files/") == NULL) {
				parts = strtok(NULL, " ");
			}
			char* relative_filepath = parts + strlen("/files/");
			
			char filepath[256];
			snprintf(filepath, 256, "/tmp/data/codecrafters.io/http-server-tester/%s", relative_filepath);
			
			FILE* file;
			file = fopen(filepath, "rb");
			if (file == NULL) {
				snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\n\r\n");
			}
			else {
				fseek(file, 0, SEEK_END);
				int file_size = ftell(file);
				rewind(file);
				char* file_buffer = (char*) malloc(file_size + 1); // size + 1 for null terminator
				file_buffer[file_size] = '\0';
				fread(file_buffer, 1, file_size, file);
				snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n%s", file_size, file_buffer);
				fclose(file);
			}
		}
		else if (strstr(buf, "GET / ") == NULL) {
			snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\n\r\n");
		}
		else {
			snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\n\r\n");
		}
		
		ssize_t send_status = send (client, response, strlen(response), 0);

		if (send_status == -1) {
			printf("Sending message to client failed: %s \n", strerror(errno));
			return 1;
		}
		free(buf);
		
		close(client);
		
		if (pid != main_pid) {
			exit(0);
		}
	}

    close(server_fd);
	return 0;
}
