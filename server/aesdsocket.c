#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_LEN 4096
#define DATAFILE_PATH "/dev/aesdchar"

static bool should_exit = false;
static int datafile_fd = -1;
static int sock_fd = -1;
static bool timer_started = false;

struct list_entry {
    pthread_t tid;
    bool complete;
    STAILQ_ENTRY(list_entry) entries;
};

STAILQ_HEAD(list_head, list_entry);

struct client_thread_args {
    int conn_fd;
    struct list_entry *entry;
};

static void signal_handler(int signum) {
    if (signum == SIGINT) {
        should_exit = true;
        if (sock_fd != -1) {
            shutdown(sock_fd, SHUT_RDWR);
            close(sock_fd);
        }
    }
}

// Reads from the client in a loop until "\n" char is received. Heap-allocated
// memory will grow indefinitely until then.
ssize_t read_line(int conn_fd, char **data_out) {
    char *data = NULL;
    size_t capacity = 0;
    size_t len = 0;

    char buf[BUF_LEN];
    while (1) {
        capacity += BUF_LEN;
        data = realloc(data, capacity);
        if (data == NULL) {
            perror("realloc");
            return -1;
        }
        ssize_t bytes_read = read(conn_fd, buf, BUF_LEN);
        if (bytes_read == -1) {
            data[0] = '\0';
            *data_out = data;
            return -1;
        }
        if (bytes_read == 0) {
            data[len] = '\0';
            *data_out = data;
            return len;
        }
        for (int i = 0; i < bytes_read; i++) {
            char c = buf[i];
            data[len] = c;
            len++;
            if (c == '\n') {
                data[len] = '\0';
                *data_out = data;
                return len;
            }
        }
    }
}

// Read bytes from `in_fd` until EOF and write them to `out_fd`.
int stream_data(int in_fd, int out_fd) {
    while (1) {
        char data[BUF_LEN];
        ssize_t bytes_read = read(in_fd, data, BUF_LEN);
        if (bytes_read == -1) {
            perror("read");
            return -1;
        }
        if (bytes_read == 0) {
            return 0;
        }
        ssize_t bytes_written = write(out_fd, data, bytes_read);
        if (bytes_written == -1) {
            perror("write");
            return -1;
        }
    }
}

void *handle_client(void *arg) {
    struct client_thread_args *thread_args = (struct client_thread_args *)arg;

    char *data;
    ssize_t data_len = read_line(thread_args->conn_fd, &data);
    if (data_len == -1) {
        return arg;
    }

    ssize_t bytes_written = write(datafile_fd, data, data_len);
    if (bytes_written == -1) {
        perror("write");
        goto cleanup1;
    }

    int data_read_fd = open(DATAFILE_PATH, O_RDONLY);
    if (data_read_fd == -1) {
        perror("open");
        goto cleanup1;
    }
    int status = stream_data(data_read_fd, thread_args->conn_fd);
    if (status == -1) {
        goto cleanup1;
    }

    status = close(thread_args->conn_fd);
    if (status == -1) {
        perror("close");
    }

cleanup1:
    free(data);
    thread_args->entry->complete = true;
    return arg;
}

// Every 10 seconds, write a RFC 2822 timestamp to the data file.
void *timed_writer(void *arg) {
    sleep(10);
    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);

    char t_buf[128];
    size_t t_len =
        strftime(t_buf, sizeof(t_buf), "%a, %d %b %Y %H:%M:%S %z", &local_tm);
    if (!t_len) {
        perror("strftime");
        return NULL;
    }

    ssize_t bytes_written =
        write(datafile_fd, "timestamp:", sizeof("timestamp:"));
    if (bytes_written == -1) {
        perror("write");
        goto cleanup;
    }
    bytes_written = write(datafile_fd, t_buf, t_len);
    if (bytes_written == -1) {
        perror("write");
        goto cleanup;
    }
    bytes_written = write(datafile_fd, "\n", 1);
    if (bytes_written == -1) {
        perror("write");
    }

cleanup:
    return NULL;
}

int start_timer_thread() {
    if (timer_started) {
        return 0;
    }
    pthread_t tid;
    int status = pthread_create(&tid, NULL, timed_writer, NULL);
    if (status != 0) {
        perror("pthread_create");
        return -1;
    }
    status = pthread_detach(tid);
    if (status != 0) {
        perror("pthread_detach");
        return -1;
    }
    timer_started = true;
    return 0;
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    int status = sigaction(SIGINT, &sa, NULL);
    if (status == -1) {
        perror("sigaction");
        return -1;
    }

    int opt;
    bool daemonize = false;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd':
            daemonize = true;
        }
    }

    if (daemonize) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }
        if (pid > 0) {
            return 0;
        }
    }

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return -1;
    }

    int opt_val = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val,
                   sizeof(opt_val)) == -1) {
        perror("setsockopt");
        return -1;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *servinfo;
    status = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (status != 0) {
        perror("getaddrinfo");
        return -1;
    }
    status = bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (status == -1) {
        perror("bind");
        return -1;
    }
    status = listen(sock_fd, 50);
    if (status == -1) {
        perror("listen");
        return -1;
    }

    datafile_fd = open(DATAFILE_PATH, O_WRONLY);
    if (datafile_fd == -1) {
        perror("open");
        return -1;
    }

    struct stat st;
    status = fstat(datafile_fd, &st);
    if (status == -1) {
        perror("fstat");
        return -1;
    }

    // Assert that the datafile is a character device file.
    assert(S_ISCHR(st.st_mode));

    freeaddrinfo(servinfo);

    struct list_head head = STAILQ_HEAD_INITIALIZER(head);
    STAILQ_INIT(&head);

    while (!should_exit) {
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(struct sockaddr);
        int conn_fd = accept(sock_fd, &client_addr, &client_addr_len);
        if (conn_fd == -1) {
            perror("accept");
            break;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sa_data), ip_str, sizeof(ip_str));
        printf("Accepted connection from %s\n", ip_str);

        struct list_entry *entry = malloc(sizeof(struct list_entry));
        if (!entry) {
            perror("malloc");
        }
        entry->complete = false;
        struct client_thread_args *thread_args =
            malloc(sizeof(struct client_thread_args));
        thread_args->entry = entry;
        thread_args->conn_fd = conn_fd;
        status = pthread_create(&entry->tid, NULL, handle_client, thread_args);
        if (status != 0) {
            perror("pthread_create");
        }
        STAILQ_INSERT_TAIL(&head, entry, entries);

        // This loop cleans up any completed threads. The looping is weird b/c
        // removing an element mid-loop invalidates the iterator. So, we break
        // the inner loop and start it over if we remove anything.
        bool thread_joined;
        do {
            thread_joined = false;
            struct list_entry *node;
            STAILQ_FOREACH(node, &head, entries) {
                if (node->complete) {
                    struct client_thread_args *thread_args = NULL;
                    int thread_status =
                        pthread_join(node->tid, (void *)thread_args);
                    if (thread_status != 0) {
                        perror("pthread_join");
                    }
                    thread_joined = true;
                    STAILQ_REMOVE(&head, node, list_entry, entries);
                    free(node);
                    free(thread_args);
                    break;
                }
            }
        } while (thread_joined);
    }

    // Final cleanup of any remaining threads.
    struct list_entry *node;
    while (!STAILQ_EMPTY(&head)) {
        node = STAILQ_FIRST(&head);
        STAILQ_REMOVE_HEAD(&head, entries);
        struct client_thread_args *thread_args = NULL;
        int thread_status = pthread_join(node->tid, (void *)thread_args);
        if (thread_status != 0) {
            perror("pthread_join");
        }
        free(node);
        free(thread_args);
    }

    return 0;
}
