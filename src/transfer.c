// TODO optimize imports in src/*
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#define BUF_SIZE 4096
#define PORT "3490" // the port client will be connecting to 

typedef uint64_t fsize_t;
typedef unsigned char filenamesize_t;

int SendFileWithSize(int file_fd, int sock_fd, void (*progress_callback)(size_t, size_t)) {
    fsize_t n_bytes;

    // get file size
    struct stat st;
    if (fstat(file_fd, &st)) {
        perror("stat");
        return 1;
    }
    fsize_t file_size = st.st_size;

    // send file size
    if ((n_bytes = send(sock_fd, (char*)&file_size, sizeof(file_size), 0)) !=
        sizeof(file_size)) {
        perror("recv");
        return 1;
    }

    char buf[BUF_SIZE];
    fsize_t offset = 0;
    while (offset < file_size) {
        n_bytes = read(file_fd, buf, MIN(BUF_SIZE - 1, file_size - offset));
        if (n_bytes == 0) {
            fprintf(stderr, "unexpected EOF\n");
            return 1;
        }
        if (n_bytes == -1) {
            perror("read");
            return 1;
        }

        offset += n_bytes;
        if (progress_callback) {
            progress_callback(offset, file_size);
        }

        if (send(sock_fd, buf, n_bytes, 0) <= 0) {
            perror("send");
            return 1;
        }
    }

    return 0;
}

int ReceiveFileWithSize(int file_fd, int sock_fd, void (*progress_callback)(size_t, size_t)) {
    int bytes_read;

    // get file size
    fsize_t file_size;
    if ((bytes_read = recv(sock_fd, (char*)&file_size, sizeof(file_size), 0)) !=
        sizeof(file_size)) {
        perror("recv");
        return 1;
    }

    // FIXME cast from fsize_t to off_t can cause overflow
    if (ftruncate(file_fd, file_size)) {
        perror("truncacte");
        return 1;
    }

    char buf[BUF_SIZE];
    fsize_t offset = 0;
    while (offset < file_size) {
        bytes_read =
            recv(sock_fd, buf, MIN(BUF_SIZE - 1, file_size - offset), 0);
        if (bytes_read == 0) {
            fprintf(stderr, "unexpected EOF\n");
            return 1;
        }
        if (bytes_read == -1) {
            perror("recv");
            return 1;
        }

        offset += bytes_read;
        if (progress_callback) {
            progress_callback(offset, file_size);
        }

        // FIXME data can be written in multiple write calls
        if (write(file_fd, buf, bytes_read) <= 0) {
            perror("write");
            return 1;
        }
    }

    return 0;
}

const char *GetTmpPath() {
    char *ret;
    if ((ret = getenv("XDG_RUNTIME_DIR"))) {
        return ret;
    }
    if ((ret = getenv("TMPDIR"))) {
        return ret;
    }
    return "/tmp";
}

void GenerateTmpFileName(char *dest) {
    sprintf(dest, "trans%d", getpid());
}

int SafeReceiveFileWithSize(char *target_path, int sock_fd, mode_t mode, void (*progress_callback)(size_t, size_t)) {
    // create temp file
    const char *tmp_path = GetTmpPath();
    size_t tmp_path_len = strlen(tmp_path);
    char tmp_file_path[tmp_path_len + FILENAME_MAX + 1];
    strcpy(tmp_file_path, tmp_path);
    tmp_file_path[tmp_path_len] = '/';
    GenerateTmpFileName(tmp_file_path + tmp_path_len + 1);

    int tmp_fd = open(tmp_file_path, O_WRONLY | O_CREAT | O_EXCL, mode);
    if (tmp_fd == -1) {
        perror("open");
        fprintf(stderr, "safe_receive: cannot create temp file at %s, aborting...\n", tmp_path);
        return 1;
    }

    // receive file
    int ret = ReceiveFileWithSize(tmp_fd, sock_fd, progress_callback);
    if (ret) {
        fprintf(stderr, "safe_receive: download failed, aborting...\n");
        close(tmp_fd);
        remove(tmp_file_path);
        return 1;
    }

    close(tmp_fd);

    // move temp file
    if (rename(tmp_file_path, target_path)) {
        if (errno == EXDEV) {
            // FIXME copy tmp_file to target_path and remove tmp_file instead of callilng system
            char cmd[5 + strlen(tmp_file_path) + strlen(target_path)];
            sprintf(cmd, "mv %s %s", tmp_file_path, target_path);
            fprintf(stderr, "%s\n", cmd);
            return system(cmd);
        }
        perror("rename");
        fprintf(stderr, "safe_receive: failed to create file, aborting...\n");
        return 1;
    }

    return 0;
}

int ConnectTo(char *hostname) {
    int sock_fd;  
    struct addrinfo hints, *servinfo, *p;
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            continue;
        }
        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "connection failed");
        return -1;
    }

    // char s[INET6_ADDRSTRLEN];
    /* inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
    printf("connecting to %s\n", s); */

    freeaddrinfo(servinfo);

    return sock_fd;
}

void *GetSocketIP(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}
