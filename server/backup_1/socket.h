#ifndef SOCKET_H
#define SOCKET_H
// socket.h
// This header file defines the functions and constants used for socket operations in the AESD socket server.
// It includes necessary libraries and defines constants for the server port, backlog, and buffer size.

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/time.h>

#define MY_PORT 9000
#define BACKLOG 10
#define AESD_SOCKET_FILE "/var/tmp/aesdsocketdata.txt"
#define BUFFER_SIZE 1024

#define LOG_SYS(fmt, ...) fprintf(stdout, "[SYS]: " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) fprintf(stderr, "[ERROR]: " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) fprintf(stdout, "[DEBUG]: " fmt "\n", ##__VA_ARGS__)

extern sig_atomic_t exit_requested; // Flag to indicate if exit is requested
extern int global_server_socket_fd;

typedef struct thread_node {
    pthread_t data_node; // Thread ID for the client connection
    struct socket_processing *sp;
    SLIST_ENTRY(thread_node) entries;
} thread_node_t;
SLIST_HEAD(thread_list_head, thread_node);

struct connection_info {
    int _sockfd; // Socket file descriptor
    struct sockaddr_in _addr; // Client address structure
    char _ip[INET_ADDRSTRLEN]; // Buffer to hold client IP address
};

struct data_packet {
    pthread_mutex_t *mutex; // Mutex to ensure thread safety
    pthread_t thread_id; // Thread ID for the client connection
    char *data; // Pointer to hold the data received from the client
    size_t length; // Length of the data received
    bool end_of_packet; // Flag to indicate end of packet
};

struct socket_processing {
    struct connection_info *connection_info; // Pointer to connection_info structure
    struct data_packet *packet; // Pointer to data_packet structure
    bool connection_active; // Flag to indicate if the connection is active
};
// Function to create a connection_info structure
// This function allocates memory for a connection_info structure and initializes it with the provided socket file
// descriptor, address, and IP address.
// Parameters:
// - sockfd: The socket file descriptor for the connection.
// - addr: Pointer to a sockaddr_in structure containing the address information of the client.
// - ip: Pointer to a character array to hold the client IP address.
// Returns: Pointer to the newly created connection_info structure, or NULL if memory allocation fails.
// Note: The caller is responsible for freeing the memory allocated for the connection_info structure using free
struct connection_info *create_connection_info(int sockfd, struct sockaddr_in *addr, char *ip);
// Function to free the memory allocated for connection_info structure
// This function deallocates the memory used by the connection_info structure.
// Parameters:
// - info: Pointer to the connection_info structure to be freed.
// Returns: None
// Note: This function should be called to avoid memory leaks after the connection is no longer needed
void free_connection_info(struct connection_info *info);

void *timestamp(void *arg); // Function to log the current timestamp every 10 seconds

// Function to handle client connections
// This function accepts incoming client connections and processes the received data.
// It reads data from the client, writes it to a file, and sends the file content
// back to the client. It also handles graceful shutdown on receiving termination signals.
// Parameters:
// - sockfd: Pointer to the socket file descriptor.
// - client_addr: Pointer to the sockaddr_in structure containing client address information.
// - client_ip: Pointer to a character array to hold the client IP address.
// Returns: None
// Note: This function runs in a loop until a termination signal is received.
void client_handler(void* connection_info);

// Function to handle socket setup
// This function creates a socket, sets socket options, and binds the socket to the specified address
// and port. It also handles errors during socket creation and binding.
// Parameters:
// - sockfd: Pointer to the socket file descriptor.
// - server_addr: Pointer to the sockaddr_in structure containing server address information.
// - server_ip: Pointer to a character array to hold the server IP address.
// Returns: None
// Note: This function should be called before starting to accept client connections.
// It exits the program if socket creation or binding fails.
// It also sets the socket to be reusable, allowing it to be bound to the same address
// even if the previous socket is still in the TIME_WAIT state.
void server_handler(void* connection_info); 
int setup_socket(void* connection_info); // Function to set up the socket and bind it to the specified address and port
void write_to_file(const char *filename, const char *data, size_t length); // Function to write data to a file
size_t read_from_file(const char *filename, char *buffer, size_t buffer_size); //
void setup_signal_handlers(); // Function to set up signal handlers for graceful shutdown
void handle_signal(int signo); // Signal handler function to handle termination signals

struct connection_info *create_connection_info(int sockfd, struct sockaddr_in *addr, char *ip);
#endif // CLIENT_H