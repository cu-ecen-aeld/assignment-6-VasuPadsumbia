#include <stdio.h>
#include "socket.h"

int main(int argc, char *argv[])
{
    setup_signal_handlers();

    struct connection_info *conn_info = malloc(sizeof(struct connection_info));
    if (!conn_info) {
        LOG_ERR("Failed to allocate connection info");
        return 1;
    }

    // Set IP and port
    memset(&conn_info->_addr, 0, sizeof(conn_info->_addr));
    conn_info->_addr.sin_family = AF_INET;
    conn_info->_addr.sin_addr.s_addr = INADDR_ANY;
    conn_info->_addr.sin_port = htons(MY_PORT);

    snprintf(conn_info->_ip, INET_ADDRSTRLEN, "0.0.0.0");
    conn_info->_sockfd = -1;

    while (!exit_requested) {
        // Create a socket and set it up
        setup_socket(conn_info);

        // Accept client connections
        int client_accepted = accept(conn_info->_sockfd, (struct sockaddr *)&conn_info->_addr, &(socklen_t){sizeof(conn_info->_addr)});
        if (client_accepted < 0) {
            LOG_ERR("Failed to accept client connection: %s", strerror(errno));
            continue; // Continue to the next iteration if accept fails
        }

        // Get client IP address
        inet_ntop(AF_INET, &conn_info->_addr.sin_addr, conn_info->_ip, INET_ADDRSTRLEN);
        LOG_SYS("Accepted connection from %s:%d", conn_info->_ip, ntohs(conn_info->_addr.sin_port));

        // Handle the client connection in a separate thread
        pthread_t thread;
        if (pthread_create(&thread, NULL, (void *)client_handler, (void *)conn_info) != 0) {
            LOG_ERR("Failed to create thread for client handler: %s", strerror(errno));
            close(client_accepted); // Close the client socket if thread creation fails
            continue; // Continue to the next iteration if thread creation fails
        }
    }

    free_connection_info(conn_info);
    return 0;
}
