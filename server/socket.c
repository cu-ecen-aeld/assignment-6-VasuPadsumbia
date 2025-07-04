#include "socket.h"

sig_atomic_t exit_requested = 0; // Flag to indicate if exit is requested
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
struct thread_list_head thread_list = SLIST_HEAD_INITIALIZER(thread_list);
time_t current_time = 0; // Variable to hold the current time for logging

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
        //buffer[bytes_read] = '\n'; // Null-terminate the buffer
        LOG_SYS("Successfully read %zu bytes from file %s", bytes_read, filename);
    }
    fclose(file); // Close the file after reading
}   
void *timestamp(void *arg) {
     (void)arg;
    current_time = time(NULL); // Initialize current_time to the current time
    char buffer[BUFFER_SIZE];
    while (!exit_requested) {
        if (time(NULL) - current_time >= 10) {
            LOG_SYS("Time since last log: %ld seconds", time(NULL) - current_time);
            current_time = time(NULL); // Update the current time after logging
            strftime(buffer, sizeof(buffer), "timestamp:%Y-%m-%d %H:%M:%S\n", localtime(&current_time)); // Format the current time
            pthread_mutex_lock(&file_mutex); // Lock the mutex for thread safety
            write_to_file(AESD_SOCKET_FILE, buffer, strlen(buffer)); // Write the formatted time to the file
            pthread_mutex_unlock(&file_mutex); // Unlock the mutex after writing
            LOG_SYS("Timestamp written to file %s", AESD_SOCKET_FILE);
        }
    }
    pthread_exit(NULL); // Exit the thread when exit is requested
}
void *data_processing(void* socket_processing) {
    struct socket_processing *sp = (struct socket_processing *)socket_processing;
    if (!sp || !sp->connection_info || !sp->packet) {
        LOG_ERR("Invalid socket processing structure");
        pthread_exit(NULL); // Exit the thread if the structure is invalid
        return NULL; // Return if the structure is invalid
    }
    int sockfd = sp->connection_info->_sockfd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    sp->connection_active = true; // Set connection_active flag to true

    while (!exit_requested && sp->connection_active) {
        bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
            LOG_ERR("Failed to receive data: %s", strerror(errno));
            //pthread_exit(NULL); // Exit the thread if receiving data fails
            //sp->connection_active = false; // Set connection_active flag to false
            break; // Return if receiving data fails
        } else if (bytes_received == 0) {
            LOG_SYS("Client disconnected %s:%d", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
            //pthread_exit(NULL); // Exit the thread if the client disconnects
            sp->connection_active = false; // Set connection_active flag to false
            break; // Return if the client disconnects 
        }
        LOG_SYS("Received %zd bytes from client %s:%d", bytes_received, sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
        pthread_mutex_lock(sp->packet->mutex); // Lock the mutex for thread safety
        sp->packet->data = (char *)realloc(sp->packet->data, sp->packet->length + bytes_received + 1);
        if (!sp->packet->data) {
            LOG_ERR("Failed to allocate memory for data: %s", strerror(errno));
            pthread_mutex_unlock(sp->packet->mutex); // Unlock the mutex before exiting
            pthread_exit(NULL); // Exit the thread if memory allocation fails
            break;    
        }

        memcpy(sp->packet->data + sp->packet->length, buffer, bytes_received);
        sp->packet->length += bytes_received;
        if (sp->packet->data[sp->packet->length] == '\n')
        {
            sp->packet->end_of_packet = true; // Set end_of_packet flag to true if newline is received
            write_to_file(AESD_SOCKET_FILE, sp->packet->data, sp->packet->length); // Write data to file
            LOG_SYS("Received %zd bytes from client %s:%d", bytes_received, sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
            LOG_DEBUG("Data: %s", sp->packet->data); // Log the received data
        }
        pthread_mutex_unlock(sp->packet->mutex); // Unlock the mutex after processing the data

        if (sp->packet->end_of_packet) {
            LOG_SYS("End of packet received from client %s:%d", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
            sp->packet->end_of_packet = false; // Reset end_of_packet flag for the next packet
            char response[BUFFER_SIZE];
            pthread_mutex_lock(sp->packet->mutex); // Lock the mutex for thread safety
            read_from_file(AESD_SOCKET_FILE, response, sizeof(response)); // Read data from file
            if (send(sockfd, response, strlen(response), 0) < 0) {
                LOG_ERR("Failed to send response to client %s:%d: %s", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port), strerror(errno));
            } else {
                LOG_SYS("Sent response to client %s:%d", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
            }
            pthread_mutex_unlock(sp->packet->mutex); // Unlock the mutex after sending the response
        }
    }    
    LOG_SYS("Closed connection with client %s:%d", sp->connection_info->_ip, ntohs(sp->connection_info->_addr.sin_port));
    free(sp->packet->data); // Free the data buffer after processing
    free(sp->packet); // Free the data packet structure
    free_connection_info(sp->connection_info); // Free the connection info structure
    free(sp); // Free the socket processing structure
    
    close(sockfd); // Close the client socket after processing    
    pthread_exit(NULL); // Exit the thread after processing the data 
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
    
    while (!exit_requested) {
        // Accept client connections
        int client_accepted = accept(conn_info->_sockfd, (struct sockaddr *)&conn_info->_addr, &(socklen_t){sizeof(conn_info->_addr)});
        if (client_accepted < 0) {
            LOG_ERR("Failed to accept client connection: %s", strerror(errno));
            continue; // Continue to the next iteration if accept fails
        }
        
        // Get client IP address
        inet_ntop(AF_INET, &conn_info->_addr.sin_addr, conn_info->_ip, INET_ADDRSTRLEN);
        LOG_SYS("Accepted connection from %s:%d", conn_info->_ip, ntohs(conn_info->_addr.sin_port));
        
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
        sp->packet->thread_id = pthread_self(); // Set the thread ID for the client
        sp->packet->data = NULL; // Initialize data pointer to NULL
        sp->packet->length = 0; // Initialize length to 0
        sp->packet->end_of_packet = false; // Initialize end_of_packet flag to false

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
        sp->connection_active = true; // Set connection_active flag to true
        pthread_create(&node->data_node, NULL, data_processing, (void *)sp); // Create a new thread for data processing
        node->sp = sp; // Set the socket processing structure in the thread node
        SLIST_INSERT_HEAD(&thread_list, node, entries); // Insert the thread node into
    }

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