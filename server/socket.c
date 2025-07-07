#include "socket.h"

sig_atomic_t exit_requested = 0; // Flag to indicate if exit is requested
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
struct thread_list_head thread_list = SLIST_HEAD_INITIALIZER(thread_list);
int global_server_socket_fd = -1; // Global variable to hold the server socket file descriptor
time_t current_time = 0; // Variable to hold the current time for logging

void free_connection_info(struct connection_info *info) {
    if (info) free(info);
}

struct connection_info *create_connection_info(int sockfd, struct sockaddr_in *addr, char *ip) {
    struct connection_info *info = (struct connection_info *)malloc(sizeof(struct connection_info));
    if (!info) {
        LOG_ERR("Failed to allocate memory for connection info: %s", strerror(errno));
        return NULL;
    }
    info->_sockfd = sockfd;
    info->_addr = *addr;
    strncpy(info->_ip, ip, INET_ADDRSTRLEN - 1);
    info->_ip[INET_ADDRSTRLEN - 1] = '\0'; // Ensure null termination
    return info;
}

void handle_signal(int signo) {
    //LOG_SYS("Received signal %d, shutting down gracefully...", signum);
    LOG_SYS("Caught signal, exiting");
    exit_requested = 1;
    if (global_server_socket_fd >= 0) {
        shutdown(global_server_socket_fd, SHUT_RDWR); // Shutdown the global server socket
        close(global_server_socket_fd); // Close the global server socket if it's open
        global_server_socket_fd = -1; // Reset the global server socket fd
    }
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);  // Add this line to ignore SIGPIPE globally
}

void write_to_file(const char *filename, const char *data, size_t length) {
    FILE *file = fopen(filename, "a");
    if (!file) {
        LOG_ERR("Failed to open file %s for writing: %s", filename, strerror(errno));
        return; // Return if file opening fails
    }
    size_t written = fwrite(data, sizeof(char), length, file);
    if (written < length) {
        LOG_ERR("Failed to write all data to file %s: %s", filename, strerror(errno));
    }

    fflush(file); // Ensure data is written to disk
    if (ferror(file)) {
        LOG_ERR("Error writing to file %s: %s", filename, strerror(errno));
    }
    if (fsync(fileno(file)) < 0) {
        LOG_ERR("Failed to sync file %s: %s", filename, strerror(errno));
    }
    fclose(file); // Close the file after writing
    //sync(); // Ensure all data is flushed to disk
}

size_t read_from_file(const char *filename, char *buffer, size_t buffer_size) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        LOG_ERR("Failed to open file %s for reading: %s", filename, strerror(errno));
        return 0; // Return if file opening fails
    }
    size_t bytes_read = fread(buffer, sizeof(char), buffer_size - 1, file);
    buffer[bytes_read] = '\0'; // Null-terminate the buffer
    fclose(file); // Close the file after reading
    return bytes_read; // Return the number of bytes read
}   

void *timestamp(void *arg) {
    (void)arg;
    char timestamp_str[BUFFER_SIZE]; // Buffer to hold the timestamp
    while (!exit_requested) {
        usleep(10000000); // Sleep for 1 second to avoid busy waiting
        current_time = time(NULL); // Get the current time
        strftime(timestamp_str, sizeof(timestamp_str), "timestamp:%Y-%m-%d %H:%M:%S\n", localtime(&current_time)); // Format the current time
        pthread_mutex_lock(&file_mutex); // Lock the mutex for thread safety
        write_to_file(AESD_SOCKET_FILE, timestamp_str, strlen(timestamp_str)); // Write the formatted time to the file
        pthread_mutex_unlock(&file_mutex); // Unlock the mutex after writing
        //LOG_SYS("Timestamp written to file %s", AESD_SOCKET_FILE);
    }
    pthread_exit(NULL); // Exit the thread when exit is requested
}

void *data_processing(void* socket_processing) {
    struct socket_processing *sp = (struct socket_processing *)socket_processing;
    if (!sp || !sp->connection_info || !sp->packet) {
        LOG_ERR("Invalid socket processing structure");
        return NULL; // Return if the structure is invalid
    }

    char buffer[BUFFER_SIZE] = {0}; // Buffer to hold received data
    ssize_t bytes_received;
    char response[BUFFER_SIZE]; // Buffer to hold the response
    while (!exit_requested && sp->connection_active) {
        bytes_received = recv(sp->connection_info->_sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
            LOG_ERR("Failed to receive data: %s", strerror(errno));
            //pthread_exit(NULL); // Exit the thread if receiving data fails
            //sp->connection_active = false; // Set connection_active flag to false
            break; // Return if receiving data fails
        } 
        
        //LOG_SYS("Received %zd bytes from client %s:%d", bytes_received, sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
        sp->packet->data = (char *)realloc(sp->packet->data, sp->packet->length + bytes_received + 1);
        if (!sp->packet->data) {
            LOG_ERR("Failed to allocate memory for data: %s", strerror(errno));
            pthread_mutex_unlock(sp->packet->mutex); // Unlock the mutex before exiting
            pthread_exit(NULL); // Exit the thread if memory allocation fails
            break;    
        }

        memcpy(sp->packet->data + sp->packet->length, buffer, bytes_received);
        sp->packet->length += bytes_received;
        sp->packet->data[sp->packet->length] = '\0'; // Null-terminate the data buffer
        //LOG_DEBUG("Data: %s", sp->packet->data); // Log the received data
        
        if (strchr(sp->packet->data, '\n'))
        {   
            sp->packet->end_of_packet = true; // Set end_of_packet flag to true if newline is received
            pthread_mutex_lock(sp->packet->mutex); // Lock the mutex for thread safety
            write_to_file(AESD_SOCKET_FILE, sp->packet->data, sp->packet->length); // Write data to file
            pthread_mutex_unlock(sp->packet->mutex); // Unlock the mutex after sending the response
            //LOG_SYS("Received %zd bytes from client %s:%d", bytes_received, sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
            //LOG_DEBUG("Data: %s", sp->packet->data); // Log the received data
        }
        if (sp->packet->end_of_packet) {
            //LOG_SYS("End of packet detected for client %s:%d", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
            sp->connection_active = false; // Set connection_active flag to false
            pthread_mutex_lock(sp->packet->mutex); // Lock the mutex for thread safety
            size_t bytes_read = read_from_file(AESD_SOCKET_FILE, response, sizeof(response)); // Read data from file
            //LOG_DEBUG("Response: %s", response); // Log the response data
            if (send(sp->connection_info->_sockfd, response, bytes_read, MSG_NOSIGNAL) < 0) {
                LOG_ERR("Failed to send response to client %s:%d: %s", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port), strerror(errno));
            }
            pthread_mutex_unlock(sp->packet->mutex); // Unlock the mutex after sending the response
        }
    }   
    //LOG_SYS("Closed connection with client %s:%d", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
    close(sp->connection_info->_sockfd); // Close the client socket
    free(sp->packet->data); // Free the data buffer
    free(sp->packet); // Free the data packet structure
    free_connection_info(sp->connection_info); // Free the connection info structure
    free(sp); // Free the socket processing structure
    pthread_exit(NULL); // Exit the thread after processing the data    
}

int setup_socket(void* connection_info) {
    struct connection_info *conn_info = (struct connection_info *)connection_info;
    if (!conn_info) {
        LOG_ERR("Invalid connection info");
        return -1; // Return if the connection info is NULL
    }
    
    conn_info->_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_info->_sockfd < 0) {
        LOG_ERR("Failed to create socket: %s", strerror(errno));
        //free_connection_info(conn_info); // Free the connection info structure
        return -1; // Return if socket creation fails
    }
    
    struct timeval timeout;
    timeout.tv_sec = 1; // Set timeout to 1 seconds
    timeout.tv_usec = 0; // Set microseconds to 0
    int opt = 1; // Option to reuse the address
    //if (setsockopt(conn_info->_sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    if (setsockopt(conn_info->_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERR("Failed to set socket options: %s", strerror(errno));
        close(conn_info->_sockfd); // Close the socket
        //free_connection_info(conn_info); // Free the connection info structure
        return -1; // Return if setting socket options fails
    }
    
    //memset(&conn_info->_addr, 0, sizeof(conn_info->_addr)); // Clear the address structure
    //conn_info->_addr.sin_family = AF_INET; // Set address family to IPv4
    //conn_info->_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any available address
    //conn_info->_addr.sin_port = htons(MY_PORT); // Set port number
    
    if (bind(conn_info->_sockfd, (struct sockaddr *)&conn_info->_addr, sizeof(conn_info->_addr)) < 0) {
        LOG_ERR("Failed to bind socket: %s", strerror(errno));
        close(conn_info->_sockfd); // Close the socket
        //free_connection_info(conn_info); // Free the connection info structure
        return -1; // Return if binding the socket fails
    }
    
    if (listen(conn_info->_sockfd, BACKLOG) < 0) {
        LOG_ERR("Failed to listen on socket: %s", strerror(errno));
        close(conn_info->_sockfd); // Close the socket
        //free_connection_info(conn_info); // Free the connection info structure
        return -1; // Return if listening on the socket fails
    }
    global_server_socket_fd = conn_info->_sockfd; // Set the global server socket file descriptor
    //LOG_SYS("Socket setup complete on port %d", MY_PORT);
    return 0;
}

void client_handler(void* connection_info) {
    struct connection_info *conn_info = (struct connection_info *)connection_info;
    if (!conn_info) {
        LOG_ERR("Invalid connection info, thread, or mutex");
        return; // Return if any of the pointers are NULL
    }   
    if (setup_socket(conn_info) != 0) {
        LOG_ERR("Failed to set up socket");
        shutdown(conn_info->_sockfd , SHUT_RDWR); // Shutdown the socket if setup fails
        close(conn_info->_sockfd); // Close the socket if setup fails
        return; // Return if socket setup fails
    }

    while (!exit_requested) {
        socklen_t addr_len = sizeof(conn_info->_addr);
        // Accept client connections
        int client_accepted = accept(conn_info->_sockfd, (struct sockaddr *)&conn_info->_addr, &addr_len);
        if (client_accepted < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // This is expected due to timeout, not a real error
                continue;  // Just loop again to check exit_requested
            }
            if (exit_requested) break; 
            LOG_ERR("Failed to accept client connection: %s", strerror(errno));
            continue; // Continue to the next iteration if accept fails
        }
        
        // Get client IP address
        inet_ntop(AF_INET, &conn_info->_addr.sin_addr, conn_info->_ip, INET_ADDRSTRLEN);
        //LOG_SYS("Accepted connection from %s:%d", conn_info->_ip, ntohs(conn_info->_addr.sin_port));
        
        // Create a new socket processing structure for the client
        struct socket_processing *sp = (struct socket_processing *)malloc(sizeof(struct socket_processing));
        if (!sp) {
            LOG_ERR("Failed to allocate memory for socket processing structure: %s", strerror(errno));
            close(client_accepted); // Close the client socket if memory allocation fails
            continue; // Continue to the next iteration if memory allocation fails
        }
        
        sp->connection_info = create_connection_info(client_accepted, &conn_info->_addr, conn_info->_ip);
        if (!sp->connection_info) {
            LOG_ERR("Failed to create connection info structure");
            free(sp); // Free the socket processing structure if connection info creation fails
            close(client_accepted); // Close the client socket if connection info creation fails
            continue; // Continue to the next iteration if connection info creation fails
        }
        
        sp->packet = (struct data_packet *)malloc(sizeof(struct data_packet));
        if (!sp->packet) {
            LOG_ERR("Failed to allocate memory for data packet: %s", strerror(errno));
            free_connection_info(sp->connection_info); // Free the connection info structure
            free(sp); // Free the socket processing structure
            close(client_accepted); // Close the client socket if data packet allocation fails
            continue; // Continue to the next iteration if data packet allocation fails
        }
        
        sp->packet->mutex = &file_mutex; // Use the global file mutex for thread safety
        sp->packet->data = NULL; // Initialize data pointer to NULL
        sp->packet->length = 0; // Initialize length to 0
        sp->connection_active = true; // Initialize connection_active flag to false
        
        thread_node_t *node = (thread_node_t *)malloc(sizeof(thread_node_t));
        if (!node) {
            LOG_ERR("Failed to allocate memory for thread node: %s", strerror(errno));
            free(sp->packet->data); // Free the data buffer if thread node allocation fails
            free(sp->packet); // Free the data packet structure if thread node allocation fails
            free_connection_info(sp->connection_info); // Free the connection info structure if thread node allocation fails
            free(sp); // Free the socket processing structure if thread node allocation fails
            close(client_accepted); // Close the client socket if thread node allocation fails
            continue; // Continue to the next iteration if thread node allocation fails
        }
        node->sp = sp; // Set the socket processing structure in the thread node
        pthread_create(&node->data_node, NULL, data_processing, (void *)sp); // Create a new thread for data processing
        SLIST_INSERT_HEAD(&thread_list, node, entries); // Insert the thread node into
    }
    close(conn_info->_sockfd); // Close the server socket when exiting
}

void server_handler(void* connection_info) {
    struct connection_info *conn_info = (struct connection_info *)connection_info;
    if (!conn_info) {
        LOG_ERR("Invalid connection info");
        return; // Return if the connection info is NULL
    }
    // Create a socket and set it up
    conn_info->_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_info->_sockfd < 0) {
        LOG_ERR("Failed to create socket: %s", strerror(errno));
        free_connection_info(conn_info); // Free the connection info structure
        return; // Return if socket creation fails
    }
    int opt = 1;
    if (setsockopt(conn_info->_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERR("Failed to set socket options: %s", strerror(errno));
        close(conn_info->_sockfd); // Close the socket
        free_connection_info(conn_info); // Free the connection info structure
        return; // Return if setting socket options fails
    }
    if (bind(conn_info->_sockfd, (struct sockaddr *)&conn_info->_addr, sizeof(conn_info->_addr)) < 0) {
        LOG_ERR("Failed to bind socket: %s", strerror(errno));
        close(conn_info->_sockfd); // Close the socket
        free_connection_info(conn_info); // Free the connection info structure
        return; // Return if binding the socket fails
    }
    close(conn_info->_sockfd); // Close the socket when exiting
    free_connection_info(conn_info); // Free the connection info structure
    LOG_SYS("Server shutdown complete");
    pthread_exit(NULL); // Exit the thread after server shutdown
}