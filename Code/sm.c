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

int sm_sock, sm_nid;

int sm_fatal(char *message) {
    fprintf(stderr, "Error: %s.\n", message);
    return -1;
}

int sm_node_init (int *argc, char **argv[], int *nodes, int *nid) {
    char *ip, buffer[1024];
    int sock, status, port;
    struct sockaddr_in address;


    /* Extract the contact information from the end of the arguments */
    ip   = strndup(argv[0][*argc - 2], 0x100);
    port = strtoul(argv[0][*argc - 1], '\0', 0xA);
    *argc -= 2;

    /* Create the socket to communicate with the allocator */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) return sm_fatal("Failed to create socket");

    address.sin_family = AF_INET;
    address.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);
    
    /* Connect to the allocator to initalize the node */
    status = connect(sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) return sm_fatal("failed to connect socket");

    /* Send an initalization request to the dsm */
    status = send(sock, "init", 5, 0);
    if (status < 4) return sm_fatal("failed to send initialization to allocator");

    /* Parse the received message to find the nid and nodes */
    status = recv(sock, buffer, 20, 0);
    if (status < 1) return sm_fatal("failed to receive initalization acknowledgement");
    sscanf(buffer, "nid: %d, nodes: %d\n", nid, nodes);

    sm_nid = *nid, sm_sock = sock;
    fflush(stdout);
    return 0;
}

void sm_node_exit (void) {
    char buffer[1024] = "\0";
    int status;

    sm_barrier();
    
    /* Send a message to the allocator to remove this node */
    snprintf(buffer, 0x10, "close node %d", sm_nid);
    status = send(sm_sock, buffer, 0x10, 0);
    if (status <= 0) {
        sm_fatal("failed to send close to allocator");
    } else {
        /* Wait for an acknowledgement */
        status = recv(sm_sock, buffer, 10, 0);
        if (status <= 0) sm_fatal("failed to receive closing acknowledgement");
    }

    fflush(stdout);
    return;
}

void *sm_malloc (size_t size) {
    return malloc(size);
}

void sm_barrier (void) {
    char buffer[1024] = "\0";
    int status;

    /* Send a message to the allocator to remove this node */
    snprintf(buffer, 8, "barrier");
    status = send(sm_sock, buffer, 8, 0);
    if (status <= 0) {
        sm_fatal("failed to send barrier to allocator");
    } else {
        /* Wait for an acknowledgement */
        status = recv(sm_sock, buffer, 12, 0);
        if (status <= 0) sm_fatal("failed to receive barrier acknowledgement");
    }

    fflush(stdout);
    return;
}

void sm_bcast (void **addr, int root_nid) {
    // TODO
    return;
}