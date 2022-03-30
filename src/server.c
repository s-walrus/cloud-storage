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

// FIXME убрать константы в enum
#define PORT "3490"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold
#define REQ_TYPE_LEN 3
#define NEW_FILE_MODE 0640

void SigchldHandler(int s) {
    // waitpid() might overwrite errno, so we save and restore it:
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

int HandleDownloadRequest(int dir_fd, int sock_fd, const char *filename) {
    int file_fd = openat(dir_fd, filename, O_RDONLY, 0);
    if (file_fd == -1) {
        perror("open");
        return 1;
    }
    // TODO handle error
    return SendFileWithSize(file_fd, sock_fd, NULL);
}

int HandleUploadRequest(int dir_fd, int sock_fd, const char *filename) {
    // TODO download to temp file, then move to actual file
    int file_fd = openat(dir_fd, filename, O_WRONLY | O_CREAT, NEW_FILE_MODE);
    if (file_fd == -1) {
        perror("open");
        return 1;
    }
    // TODO handle error
    return ReceiveFileWithSize(file_fd, sock_fd, NULL);
}

int HandleRequest(int dir_fd, int sock_fd) {
    int bytes_read;

    // get request type
    char req_type[REQ_TYPE_LEN + 1];
    if ((bytes_read = recv(sock_fd, req_type, REQ_TYPE_LEN, 0)) !=
        REQ_TYPE_LEN) {
        perror("recv");
        return 1;
    }
    req_type[REQ_TYPE_LEN] = '\0';

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
        ret = HandleUploadRequest(dir_fd, sock_fd, filename);
    } else {
        fprintf(stderr, "bad request type: %s\n", req_type);
        return 1;
    }

    return ret;
}

int main(int argc, char *argv[]) {
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;  // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    DIR *dir;
    int dir_fd;

    if (argc != 2) {
        fprintf(stderr, "usage: client DIR\n");
        return 1;
    }
    dir = opendir(argv[1]);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }
    dir_fd = dirfd(dir);
    if (dir_fd == -1) {
        perror("dirfd");
        return 1;
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;  // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
            -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ==
            -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);  // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = SigchldHandler;  // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while (1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  GetSocketIP((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (!fork()) {  // this is the child process
            close(sockfd);
            HandleRequest(dir_fd, new_fd);
            close(new_fd);
            _exit(0);
        }

        close(new_fd);
    }

    // TODO wait for forks
    closedir(dir);

    return 0;
}
