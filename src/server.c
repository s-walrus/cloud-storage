#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "transfer.c"

#define SERV_BACKLOG 10  // how many pending connections queue will hold
#define SERV_REQ_TYPE_LEN 3
#define SERV_FILE_MODE 0640

// Client-server communication is described in "transfer.c"

void SigchldHandler(int s) {
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    errno = saved_errno;
}

int IsFilenameValid(const char *filename) {
    const char *p = filename;
    while (*p != '\0' && *p != '/') {
        ++p;
    }
    if (*p == '/') {
        return 0;
    }
    return 1;
}

int HandleDownloadRequest(int dir_fd, int sock_fd, const char *file_name) {
    int file_fd = openat(dir_fd, file_name, O_RDONLY, 0);
    if (file_fd == -1) {
        perror("open");
        return 1;
    }
    return SendFileWithSize(file_fd, sock_fd, NULL);
}

int HandleUploadRequest(const char *dir_path, int sock_fd,
                        const char *file_name) {
    char file_path[strlen(dir_path) + strlen(file_name) + 2];
    strcpy(file_path, dir_path);
    strcat(file_path, "/");
    strcat(file_path, file_name);
    return SafeReceiveFileWithSize(file_path, sock_fd, SERV_FILE_MODE, NULL);
}

int HandleRequest(char *dir_path, int dir_fd, int sock_fd) {
    int bytes_read;

    // get request type
    char req_type[SERV_REQ_TYPE_LEN + 1];
    if ((bytes_read = recv(sock_fd, req_type, SERV_REQ_TYPE_LEN, 0)) !=
        SERV_REQ_TYPE_LEN) {
        perror("recv");
        return 1;
    }
    req_type[SERV_REQ_TYPE_LEN] = '\0';

    // get filename length
    filenamesize_t filename_size;
    if ((bytes_read =
             recv(sock_fd, (char *)&filename_size, sizeof(filename_size), 0)) !=
        sizeof(filename_size)) {
        perror("recv");
        return 1;
    }

    // get filename
    char filename[(size_t)filename_size + 1];
    // FIXME filename can be received with multiple recv's
    if ((bytes_read = recv(sock_fd, filename, filename_size, 0)) !=
        filename_size) {
        perror("recv");
        return 1;
    }
    filename[filename_size] = '\0';

    if (!IsFilenameValid(filename)) {
        fprintf(stderr, "bad file name: %s\n", filename);
    }

    int ret;
    if (strcmp(req_type, "DOW") == 0) {
        ret = HandleDownloadRequest(dir_fd, sock_fd, filename);
    } else if (strcmp(req_type, "UPL") == 0) {
        ret = HandleUploadRequest(dir_path, sock_fd, filename);
    } else {
        fprintf(stderr, "bad request type: %s\n", req_type);
        return 1;
    }

    return ret;
}

int main(int argc, char *argv[]) {
    char *dir_path;
    char *port;

    if (argc == 2) {
        dir_path = argv[1];
        port = TRANS_DEFAULT_PORT;
    } else if (argc == 3) {
        dir_path = argv[1];
        port = argv[2];
    } else {
        fprintf(stderr, "usage: ./server DIR [PORT]\n");
        return 1;
    }

    // assuming dir_path always points to dir (i.e. it is not woved, deleted,
    // etc.)
    // FIXME avoid using dir_path, always use DIR* instead
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }
    int dir_fd = dirfd(dir);
    if (dir_fd == -1) {
        perror("dirfd");
        return 1;
    }

    int sock_fd = ConnectTo(NULL, port);
    if (sock_fd == -1) {
        return 1;
    }
    if (listen(sock_fd, SERV_BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    printf("server: waiting for connections...\n");

    // wait for zombie-children on SIGCHLD
    struct sigaction sa;
    sa.sa_handler = SigchldHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_fd =
            accept(sock_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        char addr_s[INET6_ADDRSTRLEN];
        inet_ntop(client_addr.ss_family,
                  GetSocketIP((struct sockaddr *)&client_addr), addr_s,
                  sizeof addr_s);
        printf("server: got connection from %s\n", addr_s);

        if (!fork()) {
            close(sock_fd);
            HandleRequest(dir_path, dir_fd, new_fd);
            close(new_fd);
            _exit(0);
        }

        close(new_fd);
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    closedir(dir);
    close(sock_fd);

    return 0;
}
