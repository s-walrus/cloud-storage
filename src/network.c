#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TRANS_DEFAULT_PORT "3490"

void *GetSocketIP(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int ConnectTo(char *hostname, char *port) {
    int sock_fd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (hostname == NULL) {
        hints.ai_flags = AI_PASSIVE;
    }

    if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
            -1) {
            continue;
        }

        if (hostname == NULL) {
            int yes = 1;
            if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
                           sizeof(yes)) == -1) {
                perror("setsockopt");
                exit(1);
            }
            if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
                close(sock_fd);
                continue;
            }
        } else {
            if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
                close(sock_fd);
                continue;
            }
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "connection failed\n");
        return -1;
    }

    char s[INET6_ADDRSTRLEN];
    inet_ntop(p->ai_family, GetSocketIP((struct sockaddr *)p->ai_addr), s,
              sizeof s);
    printf("connecting to %s\n", s);

    freeaddrinfo(servinfo);

    return sock_fd;
}
