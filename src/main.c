#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <zlib.h>

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
	strncpy(request_body, headers_end + 4, sizeof(request_body) - 1);
	request_body[sizeof(request_body) - 1] = '\0';

	// Init HTTP_Request
	init_HTTP_Request(&request, HTTP_Method, RT, headers, request_body);
	return request;
}

int client_supports_gzip(const char* request_headers) {
	char* ptr = strstr(request_headers, "Accept-Encoding: ");
	if (ptr == NULL) {
		return 0;
	}
	else {
		char* compression_schemes = ptr + strlen("Accept-Encoding: ");
		char* scheme = strtok(compression_schemes, " ,");
		while(scheme != NULL) {
			if (strcmp(scheme, "gzip") == 0) {
				return 1;
			}
			scheme = strtok(NULL, " ,");
		}
		return 0;
	}
}

void add_common_headers(char* buffer, size_t size, int use_gzip, size_t content_length, const char* content_type) {
	int offset = 0;
    offset += snprintf(buffer + offset, size - offset, "Content-Type: %s\r\n", content_type);
    offset += snprintf(buffer + offset, size - offset, "Content-Length: %zu\r\n", content_length);
    if (use_gzip) {
        offset += snprintf(buffer + offset, size - offset, "Content-Encoding: gzip\r\n");
    }
}

int compress_to_gzip(const char* input, int inputSize, char* output, int outputSize) {
	z_stream zs;
	zs.zalloc = Z_NULL;
	zs.opaque = Z_NULL;
	zs.zfree = Z_NULL;
	
	if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
		return -1;
	}
	zs.avail_in = (uInt) inputSize;
	zs.next_in = (Bytef *) input; 
	
	zs.avail_out = (uInt) outputSize;
	zs.next_out = (Bytef *) output;

	int result = deflate(&zs, Z_FINISH);
	if (result != Z_STREAM_END) {
		deflateEnd(&zs);
		return -1;
	}
	if (deflateEnd(&zs) != Z_OK) {
		return -1;
	}

	return zs.total_out;
}

typedef struct  {
	int status;
	char result[20];
} StatusLine;

typedef struct {
	StatusLine status_line;
	char headers[256];
	char response_body[256];
	size_t body_length;
} HTTP_Response;

void init_HTTP_Response(HTTP_Response* response, int status, const char* result, const char* headers, const char* response_body, size_t body_len) {
	response->status_line.status = status;

	strncpy(response->status_line.result, result, sizeof(response->status_line.result) - 1);
	response->status_line.result[sizeof(response->status_line.result) - 1] = '\0';

	strncpy(response->headers, headers, sizeof(response->headers) - 1);
	response->headers[sizeof(response->headers) - 1] = '\0';
	
	if (body_len > sizeof(response -> response_body)) {
		body_len = sizeof(response -> response_body);
	}
	memcpy(response -> response_body, response_body, body_len);
	response -> body_length = body_len;
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
	while (1) {
		client_addr_len = sizeof(client_addr);
		//
		int client = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
		if (client == -1) {
			printf("Error while connecting client: %s\n", strerror(errno));
			return 1;
		}
		printf("Client connected\n");

		if (!fork()) {
			close(server_fd);
			while(1) {	
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
				strncpy(http_method, request.request_line.HTTP_Method, sizeof(http_method) - 1);
				http_method[sizeof(http_method) - 1] = '\0';

				char request_target[256] = {0};
				strncpy(request_target, request.request_line.request_target, sizeof(request_target) - 1);
				request_target[sizeof(request_target) - 1] = '\0';

				char request_headers[256] = {0};
				strncpy(request_headers, request.headers, sizeof(request_headers) - 1);
				request_headers[sizeof(request_headers) - 1] = '\0';
				int flag = client_supports_gzip(request_headers);

				char request_body[256] = {0};
				strncpy(request_body, request.request_body, sizeof(request_body) - 1);
				request_body[sizeof(request_body) - 1] = '\0';

				char response[4096];

				HTTP_Response http_response = {0};
				char response_headers[256];
				
				if (strstr(request_headers, "Connection: close") != NULL) {
					free(buf);
					break;
				}

				if (strcmp(http_method, "GET") == 0) {
					// GET request 
					if (strncmp(request_target, "/echo/", strlen("echo")) == 0) {	
						char* echo_str = strstr(request_target, "/echo/");
						echo_str += strlen("/echo/");

						if (flag == 1) {
							char compress_body[256];
							int compressed_size = compress_to_gzip(echo_str, strlen(echo_str), compress_body, sizeof(compress_body));
							if (compressed_size > 0) {
								add_common_headers(response_headers, sizeof(response_headers), flag, compressed_size, "text/plain");
								init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", response_headers, compress_body, compressed_size);
							}
							else {
								printf("Error while compressing: %s\n", strerror(errno));
								return 1;
							}
						}
						else {
							add_common_headers(response_headers, sizeof(response_headers), flag, strlen(echo_str), "text/plain");
							init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", response_headers, echo_str, strlen(echo_str));
						}
					}

					else if (strcmp(request_target, "/user-agent") == 0) {
						char* parts = strtok(request_headers, "\r\n");
						while(parts != NULL && strncmp(parts, "User-Agent: ", strlen("User-Agent: ")) != 0) {
							parts = strtok(NULL, "\r\n");
						}
						if (parts != NULL) {
							parts += strlen("User-Agent: ");
							add_common_headers(response_headers, sizeof(response_headers), flag, strlen(parts), "text/plain");
							init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", response_headers, parts, strlen(parts));
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
						strncpy(path_to_file, argv[2], sizeof(path_to_file) - 1);
						path_to_file[sizeof(path_to_file) - 1] = '\0';

						char* filename = request_target + strlen("/files/");
						char relative_filepath[256];
						size_t len = strlen(path_to_file);
						if (len > 0 && path_to_file[len - 1] != '/') {
							snprintf(relative_filepath, 256, "%s/%s", path_to_file, filename);
						}
						else {
							snprintf(relative_filepath, 256, "%s%s", path_to_file, filename);
						}

						FILE* file;
						file = fopen(relative_filepath, "rb");
						if (file == NULL) {
							init_HTTP_Response(&http_response, HTTP_STATUS_NOT_FOUND, "Not Found", "", "", 0);  
						}
						else {
							fseek(file, 0, SEEK_END);
							int file_size = ftell(file);
							rewind(file);
							char* file_buffer = (char*) malloc(file_size + 1); // size + 1 for null terminator
							file_buffer[file_size] = '\0';
							fread(file_buffer, 1, file_size, file);
							add_common_headers(response_headers, sizeof(response_headers), flag, file_size, "application/octet-stream");
							init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", response_headers, file_buffer, strlen(file_buffer));
							fclose(file);
						}
					}

					else if (strcmp(request_target, "/") == 0) {
						init_HTTP_Response(&http_response, HTTP_STATUS_OK, "OK", "", "", 0);
					}

					else {
						init_HTTP_Response(&http_response, HTTP_STATUS_NOT_FOUND, "Not Found", "", "", 0);
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
						strncpy(path_to_file, argv[2], sizeof(path_to_file) - 1);
						path_to_file[sizeof(path_to_file) - 1] = '\0';
						
						char* filename = request_target + strlen("/files/");
						char relative_filepath[256];
						size_t len = strlen(path_to_file);
						if (len > 0 && path_to_file[len - 1] != '/') {
							snprintf(relative_filepath, sizeof(relative_filepath), "%s/%s", path_to_file, filename);
						} 
						else {
							snprintf(relative_filepath, sizeof(relative_filepath), "%s%s", path_to_file, filename);
						}

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
						init_HTTP_Response(&http_response, HTTP_STATUS_CREATED, "Created", "", "", 0);	
					}
				}
				
				// Sending response to server
				char header_buf[256];
				int header_len = snprintf(header_buf, sizeof(header_buf), "%s %d %s%s%s%s", HTTP_VERSION, http_response.status_line.status, http_response.status_line.result, CRLF, 
						 http_response.headers, CRLF); 
				send(client, header_buf, header_len, 0);
				send(client, http_response.response_body, http_response.body_length, 0);
				size_t send_status = send (client, response, strlen(response), 0);

				if (send_status == -1) {
					printf("Sending message to client failed: %s \n", strerror(errno));
					return 1;
				}
				free(buf);		
			}
			close(client);
			exit(0);
		}
		close(client);
	}

    close(server_fd);
	return 0;
}
