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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/queue.h>
#include <sys/time.h>

#define PORT 9000
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define LOG_SYS(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

volatile sig_atomic_t exit_flag = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
int sockfd_global = -1;

struct thread_info {
    pthread_t thread_id;
    int client_sock;
    SLIST_ENTRY(thread_info) entries;
};

SLIST_HEAD(thread_head_s, thread_info) head = SLIST_HEAD_INITIALIZER(head);

void handle_signal(int signo) {
    (void)signo;
    exit_flag = 1;
    if (sockfd_global != -1) {
        shutdown(sockfd_global, SHUT_RDWR);
    }
    unlink(FILE_PATH);  // Remove the file if it exists
    pthread_mutex_destroy(&file_mutex);
}

void* client_handler(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    struct timeval timeout = {1, 0};
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char buffer[BUFFER_SIZE];
    ssize_t received;

    while (!exit_flag) {
        received = recv(client_sock, buffer, sizeof(buffer), 0);
        if (received < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) continue;
            break;
        } else if (received == 0) {
            break;
        }

        pthread_mutex_lock(&file_mutex);  

        FILE* file = fopen(FILE_PATH, "a");
        if (file) {
            fwrite(buffer, 1, received, file);
            fclose(file);
        }

        FILE* file_r = fopen(FILE_PATH, "r");
        if (file_r) {
            while ((received = fread(buffer, 1, sizeof(buffer), file_r)) > 0) {
                send(client_sock, buffer, received, 0);
            }
            fclose(file_r);
        }
        pthread_mutex_unlock(&file_mutex);  
        

        if (memchr(buffer, '\n', received)) break;
    }

    close(client_sock);
    return NULL;
}

void* timestamp_thread(void* arg) {
    (void)arg;
    while (!exit_flag) {
        sleep(10);

        time_t now = time(NULL);
        char timestamp[100];
        strftime(timestamp, sizeof(timestamp), "timestamp: %Y,%b,%d %H:%M:%S\n", localtime(&now));
        LOG_SYS("Writing timestamp: %s", timestamp);
        pthread_mutex_lock(&file_mutex);
        FILE* file = fopen(FILE_PATH, "a");
        if (file) {
            fwrite(timestamp, 1, strlen(timestamp), file);
            fclose(file);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);
        setsid();
        if (chdir("/") != 0) exit(EXIT_FAILURE);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) exit(EXIT_FAILURE);
    sockfd_global = sockfd;

    struct timeval timeout = {1, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    listen(sockfd, 10);

    pthread_t timer_thread;
    pthread_create(&timer_thread, NULL, timestamp_thread, NULL);

    SLIST_INIT(&head);

    while (!exit_flag) {
        int new_sock = accept(sockfd, (struct sockaddr*)&client_addr, &addr_size);
        if (new_sock < 0) {
            if (exit_flag) break;
            continue;
        }

        struct thread_info* tinfo = malloc(sizeof(struct thread_info));
        if (!tinfo) {
            close(new_sock);
            continue;
        }

        int* pclient = malloc(sizeof(int));
        if (!pclient) {
            free(tinfo);
            close(new_sock);
            continue;
        }

        *pclient = new_sock;

        pthread_create(&tinfo->thread_id, NULL, client_handler, pclient);
        SLIST_INSERT_HEAD(&head, tinfo, entries);
        if (!daemon_mode && getenv("ASSIGNMENT6_TEST")) {
            break;
        }
    }

    close(sockfd);

    struct thread_info* iter;
    while (!SLIST_EMPTY(&head)) {
        iter = SLIST_FIRST(&head);
        SLIST_REMOVE_HEAD(&head, entries);
        pthread_join(iter->thread_id, NULL);
        free(iter);
    }

    pthread_cancel(timer_thread);
    pthread_join(timer_thread, NULL);

    pthread_mutex_destroy(&file_mutex);

    unlink(FILE_PATH);

    return 0;
}
