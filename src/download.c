#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "transfer.c"
#include "progress.c"

#define NEW_FILE_MODE 0666

int main(int argc, char *argv[])
{
    char *file_name, *target_file_name, *hostname;
    if (argc == 3) {
        hostname = argv[1];
        file_name = argv[2];
        target_file_name = argv[2];
    } else if (argc == 4) {
        hostname = argv[1];
        file_name = argv[2];
        target_file_name = argv[3];
    } else {
        fprintf(stderr, "usage: ./get HOST FILE [DIST]\n");
        return 1;
    }

    int file_fd = open(target_file_name, O_WRONLY | O_CREAT, NEW_FILE_MODE);
    if (file_fd == -1) {
        perror("open");
        return 1;
    }

    int sock_fd = ConnectTo(hostname);
    if (sock_fd == -1) {
        fprintf(stderr, "failed to connect to %s\n", hostname);
        return 1;
    }

    int n_bytes;

    // send request type
    if ((n_bytes = send(sock_fd, "DOW", 3, 0)) != 3) {
        perror("send");
        // FIXME close everything
        close(sock_fd);
        return 1;
    }

    // send file name
    filenamesize_t file_name_size = strlen(file_name);
    if ((n_bytes = send(sock_fd, (char*)&file_name_size, sizeof(file_name_size), 0)) != sizeof(file_name_size)) {
        perror("send");
        return 1;
    }
    if ((n_bytes = send(sock_fd, target_file_name, file_name_size, 0)) != file_name_size) {
        perror("send");
        return 1;
    }

    // receive file
    int ret = ReceiveFileWithSize(file_fd, sock_fd, DrawProgressBar);

    close(file_fd);
    close(sock_fd);

    return ret;
}
