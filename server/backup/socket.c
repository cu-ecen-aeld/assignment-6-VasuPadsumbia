#include "socket.h"

sig_atomic_t exit_requested = 0; // Flag to indicate if exit is requested
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void free_connection_info(struct connection_info *info) {
    if (info) {
        free(info);
    }
}

struct connection_info *create_connection_info(int sockfd, struct sockaddr_in *addr, char *ip) {
    struct connection_info *info = (struct connection_info *)malloc(sizeof(struct connection_info));
    if (!info) {
        LOG_ERR("Failed to allocate memory for connection info: %s", strerror(errno));
        return NULL;
    }
    info->_sockfd = sockfd;
    info->_addr = *addr;
    strncpy(info->_ip, ip, INET_ADDRSTRLEN);
    return info;
}

void handle_signal(int signo) {
    //LOG_SYS("Received signal %d, shutting down gracefully...", signum);
    LOG_SYS("Caught signal, exiting");
    exit_requested = 1;
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
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
    } else {
        LOG_SYS("Successfully wrote %zu bytes to file %s", written, filename);
    }
    fclose(file); // Close the file after writing
}

void read_from_file(const char *filename, char *buffer, size_t buffer_size) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        LOG_ERR("Failed to open file %s for reading: %s", filename, strerror(errno));
        return; // Return if file opening fails
    }
    size_t bytes_read = fread(buffer, sizeof(char), buffer_size - 1, file);
    if (bytes_read < 0) {
        LOG_ERR("Failed to read from file %s: %s", filename, strerror(errno));
    } else {
        buffer[bytes_read] = '\0'; // Null-terminate the buffer
        LOG_SYS("Successfully read %zu bytes from file %s", bytes_read, filename);
    }
    fclose(file); // Close the file after reading
}   

void *data_processing(void* socket_processing) {
    struct socket_processing *sp = (struct socket_processing *)socket_processing;
    if (!sp || !sp->connection_info || !sp->packet) {
        LOG_ERR("Invalid socket processing structure");
        pthread_exit(NULL); // Exit the thread if the structure is invalid
        return; // Return if the structure is invalid
    }
    socklen_t addr_len = sizeof(sp->connection_info->_addr);
    int client_accepted = accept(sp->connection_info->_sockfd, (struct sockaddr *)&sp->connection_info->_addr, &addr_len);
    if (client_accepted < 0) {
        LOG_ERR("Failed to accept client connection: %s", strerror(errno));
        free_connection_info(sp->connection_info); // Free the connection info structure
        free(sp->packet->mutex); // Free the mutex if accept fails
        free(sp->packet); // Free the data packet structure
        free(sp); // Free the socket processing structure
        pthread_exit(NULL); // Exit the thread if accept fails
        return; // Return if accept fails
    }
    LOG_SYS("Accepted connection from %s:%d", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
    // Set the socket to non-blocking mode
    int flags = fcntl(client_accepted, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERR("Failed to get socket flags: %s", strerror(errno));
        close(client_accepted); // Close the client socket if fcntl fails
        free_connection_info(sp->connection_info); // Free the connection info structure
        free(sp->packet->mutex); // Free the mutex if fcntl fails
        free(sp->packet); // Free the data packet structure
        free(sp); // Free the socket processing structure
        pthread_exit(NULL); // Exit the thread if fcntl fails
        return; // Return if fcntl fails
    }
    
    sp->packet->data = (char *)malloc(BUFFER_SIZE); // Allocate memory for data
    if (!sp->packet->data) {
        LOG_ERR("Failed to allocate memory for data: %s", strerror(errno));
        close(client_accepted); // Close the client socket if memory allocation fails
        free_connection_info(sp->connection_info); // Free the connection info structure
        free(sp->packet->mutex); // Free the mutex if memory allocation fails
        free(sp->packet); // Free the data packet structure
        free(sp); // Free the socket processing structure
        pthread_exit(NULL); // Exit the thread if memory allocation fails
        return; // Return if memory allocation fails
    }
    ssize_t bytes_received;
    while (!exit_requested) {
        bytes_received = recv(client_accepted, sp->packet->data + sp->packet->length, BUFFER_SIZE - sp->packet->length, 0);
        if (bytes_received < 0) {
            if (errno == EINTR) {
                LOG_DEBUG("Receive interrupted by signal, retrying...");
                continue; // Retry receiving data if interrupted by a signal
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_DEBUG("No more data to read, exiting receive loop");
                break; // Exit the loop if no more data is available
            } else {
                LOG_ERR("Failed to receive data: %s", strerror(errno));
                close(client_accepted); // Close the client socket if receive fails
                free(sp->packet->data); // Free the data buffer if receive fails
                free_connection_info(sp->connection_info); // Free the connection info structure
                free(sp->packet->mutex); // Free the mutex if receive fails
                free(sp->packet); // Free the data packet structure
                free(sp); // Free the socket processing structure
                pthread_exit(NULL); // Exit the thread if receive fails
                return; // Return if receive fails
            }
        } else if (bytes_received == 0) {
            LOG_DEBUG("Client disconnected");
            break; // Exit the loop if the client has disconnected
        } else {
            sp->packet->length += bytes_received; // Update the length of the received data
            LOG_DEBUG("Received %zd bytes from client", bytes_received);
            // Check if the end of the packet is reached
            if (sp->packet->data[sp->packet->length - 1] == '\n') {
                sp->packet->end_of_packet = true; // Set the end_of_packet flag to true
                LOG_DEBUG("End of packet reached");
                // Process the complete packet here
                if (pthread_mutex_lock(sp->packet->mutex) != 0) {
                    LOG_ERR("Failed to lock mutex: %s", strerror(errno));
                    close(client_accepted); // Close the client socket if mutex lock fails
                    free(sp->packet->data); // Free the data buffer if mutex lock fails
                    free_connection_info(sp->connection_info); // Free the connection info structure
                    free(sp->packet->mutex); // Free the mutex if mutex lock fails
                    free(sp->packet); // Free the data packet structure
                    free(sp); // Free the socket processing structure
                    pthread_exit(NULL); // Exit the thread if mutex lock fails
                    return ; // Return if mutex lock fails
                }
                write_to_file(AESD_SOCKET_FILE, sp->packet->data, sp->packet->length); // Write the received data to the file
                if (pthread_mutex_unlock(sp->packet->mutex) != 0) {
                    LOG_ERR("Failed to unlock mutex: %s", strerror(errno));
                    close(client_accepted); // Close the client socket if mutex unlock fails
                    free(sp->packet->data); // Free the data buffer if mutex unlock fails
                    free_connection_info(sp->connection_info); // Free the connection info structure
                    free(sp->packet->mutex); // Free the mutex if mutex unlock fails
                    free(sp->packet); // Free the data packet structure
                    free(sp); // Free the socket processing structure
                    pthread_exit(NULL); // Exit the thread if mutex unlock fails
                    return ; // Return if mutex unlock fails
                }
                LOG_SYS("Processed complete packet of length %zu bytes", sp->packet->length);
            }
        }
    }
    close(client_accepted); // Close the client socket when done
    LOG_SYS("Client connection closed");
    free(sp->packet->data); // Free the data buffer after processing
    free(sp->packet->mutex); // Free the mutex after processing
    free(sp->packet); // Free the data packet structure after processing
    free(sp); // Free the socket processing structure after processing
    free_connection_info(sp->connection_info); // Free the connection info structure after processing
    pthread_exit(NULL); // Exit the thread after processing
    return ; // Return NULL to indicate thread completion
}

void setup_socket(void* connection_info) {
    struct connection_info *conn_info = (struct connection_info *)connection_info;
    if (!conn_info) {
        LOG_ERR("Invalid connection info");
        return; // Return if the connection info is NULL
    }
    
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
    
    memset(&conn_info->_addr, 0, sizeof(conn_info->_addr)); // Clear the address structure
    conn_info->_addr.sin_family = AF_INET; // Set address family to IPv4
    conn_info->_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any available address
    conn_info->_addr.sin_port = htons(MY_PORT); // Set port number
    
    if (bind(conn_info->_sockfd, (struct sockaddr *)&conn_info->_addr, sizeof(conn_info->_addr)) < 0) {
        LOG_ERR("Failed to bind socket: %s", strerror(errno));
        close(conn_info->_sockfd); // Close the socket
        free_connection_info(conn_info); // Free the connection info structure
        return; // Return if binding the socket fails
    }
    
    if (listen(conn_info->_sockfd, BACKLOG) < 0) {
        LOG_ERR("Failed to listen on socket: %s", strerror(errno));
        close(conn_info->_sockfd); // Close the socket
        free_connection_info(conn_info); // Free the connection info structure
        return; // Return if listening on the socket fails
    }
    
    LOG_SYS("Socket setup complete on port %d", MY_PORT);
}

void client_handler(void* connection_info) {
    struct connection_info *conn_info = (struct connection_info *)connection_info;
    if (!conn_info) {
        LOG_ERR("Invalid connection info, thread, or mutex");
        return; // Return if any of the pointers are NULL
    }
    setup_socket(conn_info); // Set up the socket for the connection
    struct data_packet *packet = (struct data_packet *)malloc(sizeof(struct data_packet));
    if (!packet) {
        LOG_ERR("Failed to allocate memory for data packet: %s", strerror(errno));
        free_connection_info(conn_info); // Free the connection info structure
        pthread_exit(NULL); // Exit the thread if memory allocation fails
        return; // Return if memory allocation fails
    }   
    
    packet->mutex = malloc(sizeof(pthread_mutex_t)); // Assign the mutex to the packet
    if (!packet->mutex) {
        LOG_ERR("Failed to allocate memory for mutex: %s", strerror(errno));
        free(packet); // Free the data packet structure
        free_connection_info(conn_info); // Free the connection info structure
        pthread_exit(NULL); // Exit the thread if mutex allocation fails
        return; // Return if mutex allocation fails
    }
    packet->thread_id = pthread_self(); // Set the thread ID for the client connection
    packet->data = NULL; // Initialize data pointer to NULL
    packet->length = 0; // Initialize length to 0
    packet->end_of_packet = false; // Initialize end_of_packet flag to false
    
    // Initialize the mutex for thread safety
    if (pthread_mutex_init(packet->mutex, NULL) != 0) {
        LOG_ERR("Failed to initialize mutex: %s", strerror(errno));
        free(packet); // Free the data packet structure
        free_connection_info(conn_info); // Free the connection info structure
        return; // Return if mutex initialization fails
    }

    struct socket_processing *sp = (struct socket_processing *)malloc(sizeof(struct socket_processing));
    if (!sp) {
        LOG_ERR("Failed to allocate memory for socket processing: %s", strerror(errno));
        pthread_mutex_destroy(packet->mutex); // Destroy the mutex if memory allocation fails
        free(packet->mutex); // Free the mutex if memory allocation fails
        free_connection_info(conn_info); // Free the connection info structure
        pthread_exit(NULL); // Exit the thread if memory allocation fails
        return; // Return if memory allocation fails
    }   
    sp->connection_info = conn_info;
    sp->packet = packet;
    // Handle client data processing here
    pthread_t thread; // Create a worker thread for data processing
    if (pthread_create(&thread, NULL, (void *)data_processing, (void *)sp) != 0) {
        LOG_ERR("Failed to create thread for data processing: %s", strerror(errno));
        pthread_mutex_destroy(packet->mutex); // Destroy the mutex if thread creation fails
        free(packet->mutex); // Free the mutex if thread creation fails
        free(packet); // Free the data packet structure
        free(sp); // Free the socket processing structure
        free_connection_info(conn_info); // Free the connection info structure
        pthread_exit(NULL); // Exit the thread if thread creation fails
        return; // Return if thread creation fails
    }
    LOG_SYS("Created thread %lu for client connection", thread);
    pthread_detach(thread); // Detach the thread to allow it to run independently
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