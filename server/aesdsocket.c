#include <stdio.h>
#include <signal.h>
#include "socket.h"

extern struct thread_list_head thread_list;
extern pthread_mutex_t file_mutex;
extern sig_atomic_t exit_requested;

void handle_signal_main(int signo) {
    LOG_SYS("Caught signal %d, exiting", signo);
    exit_requested = 1;
}

void setup_signal_handlers_main() {
    struct sigaction sa;
    sa.sa_handler = handle_signal_main;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int main(int argc, char *argv[]) {
    setup_signal_handlers_main();

    // Allocate and initialize server connection info
    struct connection_info *conn_info = (struct connection_info *)malloc(sizeof(struct connection_info));
    if (!conn_info) {
        LOG_ERR("Failed to allocate memory for connection info");
        return EXIT_FAILURE;
    }

    // Zero out and set up server address
    memset(&conn_info->_addr, 0, sizeof(conn_info->_addr));
    conn_info->_addr.sin_family = AF_INET;
    conn_info->_addr.sin_addr.s_addr = INADDR_ANY;
    conn_info->_addr.sin_port = htons(MY_PORT);
    snprintf(conn_info->_ip, INET_ADDRSTRLEN, "0.0.0.0");
    conn_info->_sockfd = -1; // Will be set by setup_socket()

    pthread_t timestamp_thread;
    pthread_create(&timestamp_thread, NULL, timestamp, NULL);

    // Start the main client handler loop (blocking)
    client_handler(conn_info);
    pthread_join(timestamp_thread, NULL); // Wait for the timestamp thread to finish
    // Graceful shutdown: Join all running threads
    LOG_SYS("Waiting for active client threads to finish...");
    thread_node_t *node;
    while (!SLIST_EMPTY(&thread_list)) {
        node = SLIST_FIRST(&thread_list);
        pthread_join(node->data_node, NULL);
        SLIST_REMOVE_HEAD(&thread_list, entries);
        free(node);
    }
    LOG_SYS("All client threads have finished.");
    
    // Clean up mutex
    pthread_mutex_destroy(&file_mutex);
    // Delete the socket file if it exists
    if (unlink(AESD_SOCKET_FILE) < 0 && errno != ENOENT) {
        LOG_ERR("Failed to delete socket file %s: %s", AESD_SOCKET_FILE, strerror(errno));
    } else {
        LOG_SYS("Deleted socket file %s", AESD_SOCKET_FILE);
    }
    LOG_SYS("Server shutdown complete. Exiting.");
    return EXIT_SUCCESS;
}

//int main(int argc, char *argv[])
//{
//    setup_signal_handlers();
//
//    struct connection_info *conn_info = malloc(sizeof(struct connection_info));
//    if (!conn_info) {
//        LOG_ERR("Failed to allocate connection info");
//        return 1;
//    }
//
//    // Set IP and port
//    memset(&conn_info->_addr, 0, sizeof(conn_info->_addr));
//    conn_info->_addr.sin_family = AF_INET;
//    conn_info->_addr.sin_addr.s_addr = INADDR_ANY;
//    conn_info->_addr.sin_port = htons(MY_PORT);
//
//    snprintf(conn_info->_ip, INET_ADDRSTRLEN, "0.0.0.0");
//    conn_info->_sockfd = -1;
//
//    while (!exit_requested) {
//        // Create a socket and set it up
//        setup_socket(conn_info);
//
//        // Accept client connections
//        int client_accepted = accept(conn_info->_sockfd, (struct sockaddr *)&conn_info->_addr, &(socklen_t){sizeof(conn_info->_addr)});
//        if (client_accepted < 0) {
//            LOG_ERR("Failed to accept client connection: %s", strerror(errno));
//            continue; // Continue to the next iteration if accept fails
//        }
//
//        // Get client IP address
//        inet_ntop(AF_INET, &conn_info->_addr.sin_addr, conn_info->_ip, INET_ADDRSTRLEN);
//        LOG_SYS("Accepted connection from %s:%d", conn_info->_ip, ntohs(conn_info->_addr.sin_port));
//
//        // Handle the client connection in a separate thread
//        pthread_t thread;
//        if (pthread_create(&thread, NULL, (void *)client_handler, (void *)conn_info) != 0) {
//            LOG_ERR("Failed to create thread for client handler: %s", strerror(errno));
//            close(client_accepted); // Close the client socket if thread creation fails
//            continue; // Continue to the next iteration if thread creation fails
//        }
//    }
//
//    free_connection_info(conn_info);
//    return 0;
//}
//