#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "socket.h"

extern struct thread_list_head thread_list;
extern pthread_mutex_t file_mutex;
extern sig_atomic_t exit_requested;

extern int global_server_socket_fd;

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);

    if (chdir("/") < 0) exit(EXIT_FAILURE);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
}

void handle_signal_main(int signo) {
    exit_requested = 1;
    if (global_server_socket_fd >= 0) {
        shutdown(global_server_socket_fd, SHUT_RDWR);
        close(global_server_socket_fd);
        global_server_socket_fd = -1;
    }
    //unlink(AESD_SOCKET_FILE); // Remove the socket file if it exists
}

void setup_signal_handlers_main() {
    struct sigaction sa;
    sa.sa_handler = handle_signal_main;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char *argv[]) {
    setup_signal_handlers_main();

    bool run_as_daemon = false;
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        if (opt == 'd') run_as_daemon = true;
    }
    unlink(AESD_SOCKET_FILE); // Remove the socket file if it exists
    if (run_as_daemon) {
        daemonize();
    }

    struct connection_info *conn_info = malloc(sizeof(struct connection_info));
    if (!conn_info) return EXIT_FAILURE;

    memset(&conn_info->_addr, 0, sizeof(conn_info->_addr));
    conn_info->_addr.sin_family = AF_INET;
    conn_info->_addr.sin_addr.s_addr = INADDR_ANY;
    conn_info->_addr.sin_port = htons(MY_PORT);
    snprintf(conn_info->_ip, INET_ADDRSTRLEN, "0.0.0.0");
    conn_info->_sockfd = -1;
    LOG_SYS("Connection info initialized");
    
    global_server_socket_fd = conn_info->_sockfd;

    pthread_t timestamp_thread;
    pthread_create(&timestamp_thread, NULL, timestamp, NULL);
    LOG_SYS("Timestamp thread started");
    client_handler(conn_info);
    pthread_cancel(timestamp_thread);
    if (conn_info->_sockfd >= 0) {
        close(conn_info->_sockfd);
    }

    pthread_join(timestamp_thread, NULL);

    thread_node_t *node;
    while (!SLIST_EMPTY(&thread_list)) {
        node = SLIST_FIRST(&thread_list);
        pthread_join(node->data_node, NULL);
        SLIST_REMOVE_HEAD(&thread_list, entries);
        free(node);
    }

    free_connection_info(conn_info);
    pthread_mutex_destroy(&file_mutex);

    unlink(AESD_SOCKET_FILE);

    return EXIT_SUCCESS;
}
