#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "sm.h"

int sm_sock, sm_nid;
char *sm_map;

int sm_fatal(char *message) {
    fprintf(stderr, "Error: %s.\n", message);
    if (sm_sock != 0) close(sm_sock);
    return -1;
}

int sm_node_init (int *argc, char **argv[], int *nodes, int *nid) {
    char *ip, buffer[1024] = "\0";
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
    status = recv(sock, buffer, 1023, 0);
    if (status < 1) return sm_fatal("failed to receive initalization acknowledgement");
    sscanf(buffer, "nid: %d, nodes: %d\n", nid, nodes);

    /* Map in the shared memory */
    sm_map = mmap((void *)0x6f0000000000, 0xFFFF * getpagesize(), 
                PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (sm_map == MAP_FAILED) return sm_fatal("failed to map memory");

    sm_nid = *nid, sm_sock = sock;
    fflush(stdout);
    return 0;
}

void sm_node_exit (void) {
    char buffer[1024] = "\0";
    int status;
    
    /* Send a message to the allocator to remove this node */
    snprintf(buffer, 0x20, "close node %d", sm_nid);    
    status = send(sm_sock, buffer, strlen(buffer), 0);
    if (status <= 0) {
        sm_fatal("failed to send close to allocator");
    } else {
        /* Wait for an acknowledgement */
        status = recv(sm_sock, buffer, 1023, 0);
        if (status <= 0) sm_fatal("failed to receive closing acknowledgement");
    }

    munmap(sm_map, 0xFFFF * getpagesize());
    fflush(stdout);
    return;
}

void *sm_malloc (size_t size) {
    char buffer[1024] = "\0";
    int status = 0, offset = 0;

    /* Send a message to the allocator to allocate some memory */
    snprintf(buffer, 1023, "allocate node %d size %ld", sm_nid, size);    
    status = send(sm_sock, buffer, strlen(buffer), 0);
    if (status <= 0) {
        sm_fatal("failed to send alloc to allocator");
    } else {
        memset(buffer, 0, 1024);
        /* Wait for an memory offset message */
        status = recv(sm_sock, buffer, 1023, 0);
        if (status <= 0) {
            sm_fatal("failed to receive malloc reply");
        } else {
            sscanf(buffer, "offset %d", &offset);
            fflush(stdout);
            return (void *)(sm_map + offset);
        }
    }

    fflush(stdout);
    return NULL;
}

void sm_barrier (void) {
    char buffer[1024] = "\0";
    int status;

    /* Send a message to the allocator to remove this node */
    snprintf(buffer, 1023, "barrier");
    status = send(sm_sock, buffer, strlen(buffer), 0);
    if (status <= 0) {
        sm_fatal("failed to send barrier to allocator");
    } else {
        /* Wait for an acknowledgement */
        status = recv(sm_sock, buffer, 1023, 0);
        if (status <= 0) sm_fatal("failed to receive barrier acknowledgement");
    }

    fflush(stdout);
    return;
}

void sm_bcast (void **addr, int root_nid) {
    char buffer[1024] = "\0";
    int status, value;
    char *address;

    sm_barrier();

    /* Send a message to the allocator to remove this node */
    snprintf(buffer, 1023, "cast %p val %d nid %d root %d", 
             *addr, **(char **)addr, sm_nid, root_nid);

    status = send(sm_sock, buffer, strlen(buffer), 0);
    if (status <= 0) {
        sm_fatal("failed to send cast request to allocator");
    } else {
        memset(buffer, 0, 1024);
        /* Wait for an acknowledgement */
        status = recv(sm_sock, buffer, 1023, 0);
        if (status <= 0) sm_fatal("failed to receive cast acknowledgement");
        sscanf(buffer, "address %p value %d", &address, &value);
    }

    *address = value;
    *addr = address;

    fflush(stdout);
    return;
}