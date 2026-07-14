#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CLIENTS 1024
#define BUFFER_SIZE 8192
#define HTTP_RESPONSE_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
#define HTTP_RESPONSE_403 "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"

static char root_dir[PATH_MAX];

static int set_nonblocking(SOCKET fd) {
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
}

static SOCKET create_listener(const char *addr, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        return INVALID_SOCKET;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        fprintf(stderr, "setsockopt failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    unsigned long ip = inet_addr(addr);
    if (ip == INADDR_NONE) {
        fprintf(stderr, "Invalid address: %s\n", addr);
        closesocket(sock);
        return INVALID_SOCKET;
    }
    sa.sin_addr.s_addr = ip;

    if (bind(sock, (struct sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    set_nonblocking(sock);
    return sock;
}

static void send_file(SOCKET client_fd, const char *path) {
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) {
        if (errno == EACCES) {
            send(client_fd, HTTP_RESPONSE_403, strlen(HTTP_RESPONSE_403), 0);
        } else {
            send(client_fd, HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404), 0);
        }
        closesocket(client_fd);
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        send(client_fd, HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404), 0);
        closesocket(client_fd);
        return;
    }

    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Length: %lld\r\n"
             "Connection: close\r\n"
             "Content-Type: application/octet-stream\r\n"
             "\r\n",
             (long long)st.st_size);

    send(client_fd, header, strlen(header), 0);

    char buf[BUFFER_SIZE];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        send(client_fd, buf, n, 0);
    }
    close(fd);
    closesocket(client_fd);
}

static void handle_request(SOCKET client_fd) {
    char buf[BUFFER_SIZE];
    int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        closesocket(client_fd);
        return;
    }
    buf[n] = '\0';

    char method[16], path[PATH_MAX], version[16];
    if (sscanf(buf, "%15s %255s %15s", method, path, version) != 3) {
        closesocket(client_fd);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        closesocket(client_fd);
        return;
    }

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s%s", root_dir, path);
    if (path[strlen(path)-1] == '/') {
        strncat(full_path, "index.html", sizeof(full_path) - strlen(full_path) - 1);
    }

    char resolved[PATH_MAX];
    if (_fullpath(resolved, full_path, PATH_MAX) == NULL) {
        send(client_fd, HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404), 0);
        closesocket(client_fd);
        return;
    }
    if (strncmp(resolved, root_dir, strlen(root_dir)) != 0) {
        send(client_fd, HTTP_RESPONSE_403, strlen(HTTP_RESPONSE_403), 0);
        closesocket(client_fd);
        return;
    }

    send_file(client_fd, resolved);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <addr:port> <root_dir>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char addr[64];
    int port;
    if (sscanf(argv[1], "%63[^:]:%d", addr, &port) != 2) {
        fprintf(stderr, "Invalid address:port format\n");
        return EXIT_FAILURE;
    }

    if (_fullpath(root_dir, argv[2], PATH_MAX) == NULL) {
        perror("_fullpath root_dir");
        return EXIT_FAILURE;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }

    SOCKET listen_fd = create_listener(addr, port);
    if (listen_fd == INVALID_SOCKET) {
        WSACleanup();
        return EXIT_FAILURE;
    }

    printf("Serving files from %s on http://%s:%d/\n", root_dir, addr, port);

    fd_set readfds;
    SOCKET client_sockets[MAX_CLIENTS];
    int client_count = 0;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        SOCKET max_fd = listen_fd;
        for (int i = 0; i < client_count; i++) {
            FD_SET(client_sockets[i], &readfds);
            if (client_sockets[i] > max_fd) max_fd = client_sockets[i];
        }

        struct timeval tv = {1, 0};
        int rc = select((int)max_fd + 1, &readfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (WSAGetLastError() == WSAEINTR) continue;
            fprintf(stderr, "select failed: %d\n", WSAGetLastError());
            break;
        }
        if (rc == 0) continue;

        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_in client_addr;
            int len = sizeof(client_addr);
            SOCKET client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len);
            if (client_fd != INVALID_SOCKET) {
                set_nonblocking(client_fd);
                if (client_count < MAX_CLIENTS) {
                    client_sockets[client_count++] = client_fd;
                } else {
                    closesocket(client_fd);
                }
            }
        }

        for (int i = 0; i < client_count; i++) {
            if (FD_ISSET(client_sockets[i], &readfds)) {
                handle_request(client_sockets[i]);
                for (int j = i; j < client_count - 1; j++) {
                    client_sockets[j] = client_sockets[j+1];
                }
                client_count--;
                i--;
            }
        }
    }

    closesocket(listen_fd);
    WSACleanup();
    return 0;
}