#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_LEN 4096

int sock_fd = -1;

static void handler(int signum) {
    if (signum == SIGINT) {
        if (sock_fd != -1) {
            shutdown(sock_fd, SHUT_RDWR);
            close(sock_fd);
        }
        exit(128 + signum);
    }
}

ssize_t read_line(int conn_fd, char **data_out) {
    char *data = NULL;
    size_t capacity = 0;
    size_t len = 0;

    char buf[BUF_LEN];
    while (1) {
        capacity += BUF_LEN;
        data = realloc(data, capacity);
        printf("data: %p\n", (void *)data);
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

int main(int argc, char *argv[]) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
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
    status = listen(sock_fd, 0);
    if (status == -1) {
        perror("listen");
        return -1;
    }

    int data_fd =
        open("/var/tmp/aesdsocketdata", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (data_fd == -1) {
        perror("open");
        return -1;
    }

    freeaddrinfo(servinfo);

    while (1) {
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(struct sockaddr);
        int conn_fd = accept(sock_fd, &client_addr, &client_addr_len);
        if (conn_fd == -1) {
            perror("accept");
            return -1;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sa_data), ip_str, sizeof(ip_str));
        printf("Accepted connection from %s\n", ip_str);

        char *data;
        ssize_t data_len = read_line(conn_fd, &data);
        if (data_len == -1) {
            return -1;
        }
        printf("got data: %s\n", data);

        ssize_t bytes_written = write(data_fd, data, data_len);
        if (bytes_written == -1) {
            perror("write");
            return -1;
        }
        free(data);

        int data_read_fd = open("/var/tmp/aesdsocketdata", O_RDONLY);
        if (data_read_fd == -1) {
            perror("open");
            return -1;
        }
        int status = stream_data(data_read_fd, conn_fd);
        if (status == -1) {
            return -1;
        }

        status = close(conn_fd);
        if (status == -1) {
            perror("close");
            return -1;
        }
    }
    return 0;
}
