#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>

#include "sm.h"

int sm_node_init (int *argc, char **argv[], int *nodes, int *nid) {
    char *ip, buffer[1024];
    int sock, status, port;
    struct sockaddr_in address;

    /* Extract the contact information from the end of the arguments */
    ip   = strndup(argv[0][*argc - 2], 0x100);
    port = strtoul(argv[0][*argc - 1], '\0', 0xA);
    *argc -= 2;

    /* Create the socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) {
        fprintf(stderr, "Failed to create socket.\n");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    status = connect(sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) {
        fprintf(stderr, "Failed to connect socket.\n");
        exit(EXIT_FAILURE);
    }

    /* Send an initalization request to the dsm */
    send(sock, "init", 5, 0);
    read(sock, buffer, 20);
    //sscanf(buffer, "nodes: %d, nid: %d\n", nodes, nid);

    return 0;
}

void sm_node_exit (void) {
    return;
}

void *sm_malloc (size_t size) {
    return malloc(size);
}

void sm_barrier (void) {
    return;
}

void sm_bcast (void **addr, int root_nid) {\
    // TODO
    return;
}