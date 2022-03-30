#include <inttypes.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#define BUF_SIZE 256
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
