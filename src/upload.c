#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "progress.c"
#include "transfer.c"
#include "network.c"

// Client-server communication is described in "transfer.c"

char *GetFileName(char *path) {
    char *p = path + strlen(path);
    while (p != path && *(p - 1) != '/') {
        --p;
    }
    return p;
}

int main(int argc, char *argv[]) {
    // TODO specify port by -p
    char *file_name, *target_file_name, *hostname, *port;
    if (argc == 3) {
        hostname = argv[1];
        file_name = argv[2];
        target_file_name = GetFileName(argv[2]);
        port = TRANS_DEFAULT_PORT;
    } else if (argc == 4) {
        hostname = argv[1];
        file_name = argv[2];
        target_file_name = argv[3];
        port = TRANS_DEFAULT_PORT;
    } else if (argc == 5) {
        hostname = argv[1];
        file_name = argv[2];
        target_file_name = argv[3];
        port = argv[4];
    } else {
        fprintf(stderr, "usage: ./send HOST FILE [DIST] [PORT]\n");
        return 1;
    }

    int file_fd = open(file_name, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return 1;
    }

    int sock_fd = ConnectTo(hostname, port);
    if (sock_fd == -1) {
        fprintf(stderr, "failed to connect to %s\n", hostname);
        return 1;
    }

    int n_bytes;

    // send request type
    if ((n_bytes = send(sock_fd, "UPL", 3, 0)) != 3) {
        perror("send");
        // FIXME close everything
        close(sock_fd);
        return 1;
    }

    // send file name
    filenamesize_t file_name_size = strlen(target_file_name);
    if ((n_bytes = send(sock_fd, (char *)&file_name_size,
                        sizeof(file_name_size), 0)) != sizeof(file_name_size)) {
        perror("send");
        return 1;
    }
    if ((n_bytes = send(sock_fd, target_file_name, file_name_size, 0)) !=
        file_name_size) {
        perror("send");
        return 1;
    }

    // send file
    int ret = SendFileWithSize(file_fd, sock_fd, DrawProgressBar);

    close(file_fd);
    close(sock_fd);

    return ret;
}
