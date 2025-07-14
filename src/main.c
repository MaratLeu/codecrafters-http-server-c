#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define CRLF "\r\n"
#define HTTP_VERSION "HTTP/1.1"
#define HTTP_STATUS_OK 200
#define HTTP_STATUS_NOT_FOUND 404
#define HTTP_STATUS_CREATED 201
#define CT "Content-Type:" // Header that specifies the format of the response body
#define CL "Content-Length:" // Header that specifies the size of the response body, in bytes

typedef struct {
	char HTTP_Method[10];
	char request_target[256];
} RequestLine;

typedef struct {
	RequestLine request_line;
	char headers[256];
	char request_body[256];
} HTTP_Request;

void init_HTTP_Request(HTTP_Request* request, const char* method, const char* target, const char* headers, const char* request_body) 
{
	strncpy(request->request_line.HTTP_Method, method, sizeof(request->request_line.HTTP_Method) - 1);
	request->request_line.HTTP_Method[sizeof(request->request_line.HTTP_Method) - 1] = '\0';

	strncpy(request->request_line.request_target, target, sizeof(request->request_line.request_target) - 1);
	request->request_line.request_target[sizeof(request->request_line.request_target) - 1] = '\0';
	
	strncpy(request->headers, headers, sizeof(request->headers) - 1);
	request->headers[sizeof(request->headers) - 1] = '\0';
	
	strncpy(request->request_body, request_body, sizeof(request->request_body) - 1);
	request->request_body[sizeof(request->request_body) - 1] = '\0';
}

HTTP_Request parse_request (char* buf) {
	// An exapmle of HTTP Request 
	// GET /index.html HTTP/1.1\r\nHost: localhost:4221\r\nUser-Agent: cur	l/7.64.1\r\nAccept: */*\r\n\r\n
	HTTP_Request request = {0};
	if (buf == NULL) return request;

	// Get Request Line (RL)
	char* sub_str = strstr(buf, "\r\n"); 
	if (sub_str == NULL) return request;
	size_t size_of_RL = sub_str - buf;
    char request_line[256] = {0};
	strncpy(request_line, buf, size_of_RL);       
	
	// Get HTTP Method
	char* first_space = strchr(request_line, ' ');
	if (first_space == NULL) return request;
	size_t size_of_method = first_space - request_line;
    char HTTP_Method[10] = {0};
	strncpy(HTTP_Method, request_line, size_of_method);
	
	// Get Request Target (RT)
	char* second_space = strchr(first_space + 1, ' '); // +1 для того чтобы не указывало на первый пробел
	if (second_space == NULL) return request;
	size_t size_of_RT = second_space - (first_space + 1);
	char RT[256] = {0};
	strncpy(RT, first_space + 1, size_of_RT);
	
	// Get Headers and RequestBody
	char* headers_end = strstr(sub_str, "\r\n\r\n"); // end of headers cause headers ended with 2 CRLF
	if (headers_end == NULL) return request;
	size_t size_of_headers = headers_end - (sub_str + 2);
	char headers[256] = {0};
	strncpy(headers, sub_str + 2, size_of_headers);
	
	char request_body[256] = {0};
	strcpy(request_body, headers_end + 4);

	// Init HTTP_Request
	init_HTTP_Request(&request, HTTP_Method, RT, headers, request_body);
	return request;
}

typedef struct  {
	int status;
	char result[20];
} StatusLine;

typedef struct {
	StatusLine status_line;
	char headers[256];
	char response_body[256];
} HTTP_Response;

void init_HTTP_Response(HTTP_Response* response, int status, const char* result, const char* headers, const char* response_body) {
	response->status_line.status = status;

	strncpy(response->status_line.result, result, sizeof(response->status_line.result) - 1);
	response->status_line.result[sizeof(response->status_line.result) - 1] = '\0';

	strncpy(response->headers, headers, sizeof(response->headers) - 1);
	response->headers[sizeof(response->headers) - 1] = '\0';

	strncpy(response->response_body, response_body, sizeof(response->response_body) - 1);
	response->response_body[sizeof(response->response_body) - 1] = '\0';
}
	

int main(int argc, char* argv[]) {
    
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
	//
	server_fd = socket(AF_INET, SOCK_STREAM, 0);  
	// socket((int)domain, (int)type, (int)protocol) AF_INET: IPv4, AF_INET6: IPv6, SOCK_STREAM: TCP, SOCK_DGRAM(UDP)
	
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

		char* buf = (char*) malloc(1024);

		int num_bytes_recv = recv(client, buf, 1024 - 1, 0);
		if (num_bytes_recv <= 0) {
			printf("Received message failed: %s \n", strerror(errno));
			return 1;
		}
		buf[num_bytes_recv] = '\0';
		
		// Parse HTTP Request
		HTTP_Request request = parse_request(buf);
		char http_method[10] = {0};
		strcpy(http_method, request.request_line.HTTP_Method);
		
		char request_target[256] = {0};
		strcpy(request_target, request.request_line.request_target);
	
		char request_headers[256] = {0};
		strcpy(request_headers, request.headers);

		char request_body[256] = {0};
		strcpy(request_body, request.request_body);

		char response[4096];

		HTTP_Response http_response = {0};
		char response_headers[256];
		if (strcmp(http_method, "GET") == 0) {
			// GET request 
			if (strncmp(request_target, "/echo/", strlen("echo")) == 0) {	
				char* echo_str = strstr(request_target, "/echo/");
				echo_str += strlen("/echo/");

				snprintf(response_headers, 256, "%s text/plain%s%s %d%s", CT, CRLF, CL, (int)strlen(echo_str), CRLF);
				init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", response_headers, echo_str);
			}

			else if (strcmp(request_target, "/user-agent") == 0) {
				char* parts = strtok(request_headers, "\r\n");
				while(parts != NULL && strncmp(parts, "User-Agent: ", strlen("User-Agent: ")) != 0) {
					parts = strtok(NULL, "\r\n");
				}
				if (parts != NULL) {
					parts += strlen("User-Agent: ");
					snprintf(response_headers, 256, "%s text/plain%s%s %d%s", CT, CRLF, CL, (int)strlen(parts), CRLF);
					init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", response_headers, parts);
				}
				else {
					printf("User-Agent value not found\n");
					return 1;
				}
			}

			else if (strncmp(request_target, "/files/", strlen("/files/")) == 0) {
				if (argc < 3) {
					printf("Not enough arguments\n");
					return 1;
				}
				
				if (strcmp(argv[1], "--directory") != 0) {
					printf("There is no flag directory\n");
					return 1;
				}

				char path_to_file[256];
				strcpy(path_to_file, argv[2]);

				char* filename = request_target + strlen("/files/");
				char relative_filepath[256];
				snprintf(relative_filepath, 256, "%s%s", path_to_file, filename);
				
				FILE* file;
				file = fopen(relative_filepath, "rb");
				if (file == NULL) {
					init_HTTP_Response(&http_response, HTTP_STATUS_NOT_FOUND, "Not Found", "", "");  
				}
				else {
					fseek(file, 0, SEEK_END);
					int file_size = ftell(file);
					rewind(file);
					char* file_buffer = (char*) malloc(file_size + 1); // size + 1 for null terminator
					file_buffer[file_size] = '\0';
					fread(file_buffer, 1, file_size, file);
					snprintf(response_headers, 256, "%s application/octet-stream%s%s %d%s", CT, CRLF, CL, file_size, CRLF);
					init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", response_headers, file_buffer);
					fclose(file);
				}
			}

			else if (strcmp(request_target, "/") == 0) {
				init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", "", "");
			}

			else {
				init_HTTP_Response(&http_response, HTTP_STATUS_NOT_FOUND, "Not Found", "", "");
			}
		}
		
		else if (strcmp(http_method, "POST") == 0) {
			if (strncmp(request_target, "/files/", strlen("/files/")) == 0) {	
				if (argc < 3) {
					printf("Not enough arguments\n");
					return 1;
				}
				
				if (strcmp(argv[1], "--directory") != 0) {
					printf("There is no flag directory\n");
					return 1;
				}

				char path_to_file[256];
				strcpy(path_to_file, argv[2]);
				
				char* filename = request_target + strlen("/files/");
				char relative_filepath[256];
				snprintf(relative_filepath, 256, "%s%s", path_to_file, filename);
				
				FILE* file;
				file = fopen(relative_filepath, "wb");
				if (file == NULL) {
					printf("Opening file failed: %s\n", strerror(errno));
				}
				size_t written_content = fwrite(request_body, sizeof(char), strlen(request_body), file);
				fclose(file);
				if (written_content != strlen(request_body)) {
					printf("Recording data to file failed: %s \n", strerror(errno));
				}
				init_HTTP_Response(&http_response, HTTP_STATUS_CREATED, "Created", "", "");	
			}
		}
		
		snprintf(response, 4096, "%s %d %s%s%s%s%s", HTTP_VERSION, http_response.status_line.status, http_response.status_line.result, CRLF, 
				 http_response.headers, CRLF, http_response.response_body); 
		size_t send_status = send (client, response, strlen(response), 0);

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
