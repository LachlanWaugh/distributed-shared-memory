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
#include <signal.h>

#include "sm.h"

int sm_sock, sm_nid;
char *sm_map;

int sm_fatal(char *message) {
    fprintf(stderr, "Error: %s.\n", message);
    if (sm_sock != 0)
        close(sm_sock);

    fflush(stdout);
    return -1;
}

void sm_segv(int signum, siginfo_t *si, void *ctx) {
    char buffer[1024] = "\0";
    int value, size;

    fprintf(stderr, "ERROR: %d %p\n", sm_nid, si->si_addr);

    /* Find the offset of the variable from the memory base */
    long offset = (char *) si->si_addr - sm_map;

    /* Send a message to the allocator to find the value at the address */
    snprintf(buffer, 1023, "read fault: node %d offset %ld", sm_nid, offset);
    send(sm_sock, buffer, strlen(buffer), 0);

    memset(buffer, 0, 1023);
    /* Wait for the value to be returned */
    recv(sm_sock, buffer, 1023, 0);
    sscanf(buffer, "size %d, value %d", &size, &value);

    mprotect(si->si_addr, size, PROT_READ);
    *(char *)(si->si_addr) = value;

    return;
}

void sm_poll(int signum) {
    char buffer[1024] = "\0";
    int offset, size;

    recv(sm_sock, buffer, 1023, MSG_PEEK);

    /* Handle a read request for a memory address */
    if (strstr(buffer, "request")) {
        recv(sm_sock, buffer, 1023, 0);
        sscanf(buffer, "request %d", &offset);
        
        /* Send the request value back */
        snprintf(buffer, 1023, "value %d", *(sm_map + offset));
        send(sm_sock, buffer, strlen(buffer), 0);

    /* Handle a loss of write permissions */
    } else if (strstr(buffer, "invalidate")) {
        recv(sm_sock, buffer, 1023, 0);
        sscanf(buffer, "invalidate %d size %d", &offset, &size);
        
        /* Invalidate the required memory and send an acknowledgement */
        mprotect(sm_map + offset, size, PROT_NONE);
        snprintf(buffer, 1023, "invalidate ACK");
        send(sm_sock, buffer, strlen(buffer), 0);
    }
}

int sm_node_init (int *argc, char **argv[], int *nodes, int *nid) {
    char *ip, buffer[1024] = "\0";
    int sock, status, port;

    /* Extract the contact information from the end of the arguments */
    ip   = strndup(argv[0][*argc - 2], 0x100);
    port = strtoul(argv[0][*argc - 1], '\0', 0xA);
    *argc -= 2;

    /* Create the socket to communicate with the allocator */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) return sm_fatal("Failed to create socket");

    struct sockaddr_in address;
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
                PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (sm_map == MAP_FAILED) return sm_fatal("failed to map memory");

    /* enable SIGPOLL on the socket */
    fcntl(sock, F_SETFL, O_ASYNC);
    fcntl(sock, F_SETOWN, getpid());
    /* Create the handler for SIGPOLL */
    struct sigaction sa_poll;
    sa_poll.sa_handler  = sm_poll;
    sa_poll.sa_flags    = SA_RESTART;
    sigemptyset(&sa_poll.sa_mask);
    sigaction(SIGPOLL, &sa_poll, NULL);

    /* Create the signal handler */
    struct sigaction sa;
    sa.sa_sigaction = sm_segv;
    sa.sa_flags     = SA_SIGINFO|SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);

    sm_nid = *nid, sm_sock = sock;
    fflush(stdout);
    return 0;
}

void sm_node_exit (void) {
    char buffer[1024] = "\0";
    int status;
    
    sm_barrier();

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
        return NULL;
    }
        
    memset(buffer, 0, 1024);
    /* Wait for an memory offset message */
    status = recv(sm_sock, buffer, 1023, 0);
    if (status <= 0) {
        sm_fatal("failed to receive malloc reply");
        return NULL;
    }

    sscanf(buffer, "offset %d", &offset);
    mprotect(sm_map + offset, size, PROT_READ|PROT_WRITE);
    memset(sm_map+offset, 0, size);

    fflush(stdout);
    return (void *)(sm_map + offset);
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
        if (status <= 0) {
            sm_fatal("failed to receive barrier acknowledgement");
        } else if (strcmp(buffer, "barrier ACK")) {
            sm_fatal("invalid barrier acknowledgement received");
        }
    }

    fflush(stdout);
    return;
}

void sm_bcast (void **addr, int root_nid) {
    char buffer[1024] = "\0", *address;
    int status;

    sm_barrier();

    /* Send a message to the allocator to cast the address */
    snprintf(buffer, 1023, "cast %p nid %d root %d", 
             *addr, sm_nid, root_nid);

    status = send(sm_sock, buffer, strlen(buffer), 0);
    if (status <= 0) {
        sm_fatal("failed to send cast request to allocator");
    } else {
        memset(buffer, 0, 1024);
        /* Wait for an acknowledgement */
        status = recv(sm_sock, buffer, 1023, 0);
        if (status <= 0)
            sm_fatal("failed to receive cast acknowledgement");

        sscanf(buffer, "address %p", &address);
    }

    *addr = address;
    fflush(stdout);
    return;
}