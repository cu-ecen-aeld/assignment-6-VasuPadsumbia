#include <stdio.h>
#include <signal.h>
#include "socket.h"

extern struct thread_list_head thread_list;
extern pthread_mutex_t file_mutex;
extern sig_atomic_t exit_requested;
int global_server_socket_fd = -1; // Initialize global server socket fd

void daemonize()
{
    pid_t pid;

    // Fork the first time
    pid = fork();
    if (pid < 0) {
        LOG_ERR("First fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // Parent exits
        exit(EXIT_SUCCESS);
    }

    // Create new session
    if (setsid() < 0) {
        LOG_ERR("setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Fork again to ensure no controlling terminal
    pid = fork();
    if (pid < 0) {
        LOG_ERR("Second fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // First child exits
        exit(EXIT_SUCCESS);
    }

    // Set file permissions mask
    umask(0);

    // Change working directory to root
    if (chdir("/") < 0) {
        LOG_ERR("Failed to change directory to / : %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect std descriptors to /dev/null
    open("/dev/null", O_RDONLY); // stdin
    open("/dev/null", O_WRONLY); // stdout
    open("/dev/null", O_WRONLY); // stderr

    LOG_SYS("Daemon process started (PID: %d)", getpid());
}

void handle_signal_main(int signo) {
    LOG_SYS("Caught signal %d, exiting", signo);
    exit_requested = 1;
    if (global_server_socket_fd >= 0) {
        shutdown(global_server_socket_fd, SHUT_RDWR);
        close(global_server_socket_fd);
        global_server_socket_fd = -1; // Reset global server socket fd
        LOG_SYS("Closed global server socket %d", global_server_socket_fd);
    } else {
        LOG_ERR("Global server socket was not initialized or already closed");
    }
}

void setup_signal_handlers_main() {
    struct sigaction sa;
    sa.sa_handler = handle_signal_main;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);  // Add this line to ignore SIGPIPE globally
}

int main(int argc, char *argv[]) {
    setup_signal_handlers_main();

    bool run_as_daemon = false;
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        if (opt == 'd') run_as_daemon = true;
    }

    if (run_as_daemon) {
        daemonize();
    }

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
    global_server_socket_fd = conn_info->_sockfd; // Initialize global server socket fd

    pthread_t timestamp_thread;
    pthread_create(&timestamp_thread, NULL, timestamp, NULL);

    // Start the main client handler loop (blocking)
    client_handler(conn_info);
    if (conn_info->_sockfd >= 0) {
        close(conn_info->_sockfd);
        //LOG_SYS("Closed server socket %d", conn_info->_sockfd);
    } else {
        LOG_ERR("Server socket was not initialized or already closed");
    }
    pthread_join(timestamp_thread, NULL); // Wait for the timestamp thread to finish
    // Graceful shutdown: Join all running threads
    //LOG_SYS("Waiting for active client threads to finish...");
    thread_node_t *node;
    while (!SLIST_EMPTY(&thread_list)) {
        node = SLIST_FIRST(&thread_list);
        pthread_join(node->data_node, NULL);
        SLIST_REMOVE_HEAD(&thread_list, entries);
        free(node);
    }

    //LOG_SYS("All client threads have finished.");
    // Close the server socket
    free_connection_info(conn_info); // Free the connection info structure
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
