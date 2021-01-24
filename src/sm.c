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
#include "config.h"
#include "sm_message.h"

int sm_sock, sm_nid;
char *sm_map;
// sm_page_table[];

int sm_fatal(char *message) {
    fprintf(stderr, "Error: %s.\n", message);
    if (sm_sock != 0) close(sm_sock);

    fflush(stdout);
    return -1;
}

void sm_segv(int signum, siginfo_t *si, void *ctx) {
    char buffer[1024] = "\0";
    int value, size;

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
    char buffer[4196] = "\0";
    int offset, status;
 
    recv(sm_sock, buffer, 1023, MSG_PEEK);

    /* Handle a read request for a memory address */
    if (strstr(buffer, "request read")) {
        status = recv(sm_sock, buffer, 1023, 0);
        if (status <= 0) sm_fatal("failed to receive read request");
        sscanf(buffer, "request %d", &offset);

        /* Send the request page back */
        snprintf(buffer, 4096 + 6, "page: %s", sm_map + offset);
        status = send(sm_sock, buffer, strlen(buffer), 0);
        if (status <= 0) sm_fatal("failed to send page to allocator");

    /* Handle a loss of write permissions */
    } else if (strstr(buffer, "request write")) {
        status = recv(sm_sock, buffer, 1023, 0);
        if (status <= 0) sm_fatal("failed to receive write request");
        sscanf(buffer, "write request: %d", &offset);

        /* Invalidate the required memory and send an acknowledgement */
        mprotect(sm_map + offset, getpagesize(), PROT_NONE);

        snprintf(buffer, 1023, "invalidate ACK");
        status = send(sm_sock, buffer, strlen(buffer), 0);
        if (status <= 0) sm_fatal("failed to send invalidation acknowledgement to allocator");
    }

    return;
}

int socket_init(char *ip, int port) {
    int status = 0;    

    /* Create the socket to communicate with the allocator */
    sm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sm_sock == 0) {
        return sm_fatal("Failed to create socket");
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    /* Connect to the allocator to initalize the node */
    status = connect(sm_sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) return sm_fatal("failed to connect socket");
}

int handler_init() {
    /* enable SIGPOLL on the socket */
    fcntl(sm_sock, F_SETFL, O_ASYNC);
    fcntl(sm_sock, F_SETOWN, getpid());

    /* Create the handler for POLL */
    struct sigaction sa;
    sa.sa_handler = sm_poll;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGIO, &sa, NULL);

    /* Create the handler for SEGV */
    sa.sa_sigaction = sm_segv;
    sa.sa_flags     = SA_SIGINFO|SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);

    return 0;
}

int sm_node_init (int *argc, char **argv[], int *nodes, int *nid) {
    char *ip;
    int status, port;
    msg_t *message;

    /* Extract the contact information from the end of the arguments */
    ip   = strndup(argv[0][*argc - 2], 0x4);
    port = strtoul(argv[0][*argc - 1], '\0', 10);
    *argc -= 2;

    socket_init(ip, port);
    handler_init();

    /* Send an initalization request to the dsm */
    status = sm_send(sm_sock, SM_INIT, NULL);
    if (status) {
        return sm_fatal("failed to send initialization to allocator");
    }

    /* Parse the received message to find the nid and nodes */
    status = sm_recv(sm_sock, &message);
    if (status || message->type != SM_INIT_REPLY) {
        return sm_fatal("failed to receive initalization acknowledgement");
    } else {
        *nid = sm_nid = (int) message->nid;
        *nodes = 7777;
    }

    /* Map in the shared memory */
    sm_map = mmap((void *)SM_MAP_START, SM_NUM_PAGES * getpagesize(), 
                PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (sm_map == MAP_FAILED) return sm_fatal("failed to map memory");

    fflush(stdout);
    return 0;
}

void sm_node_exit (void) {
    msg_t *message;
    int status;

    fflush(NULL);
    sm_barrier();

    /* Send a message to the allocator to remove this node */  
    status = sm_send(sm_nid, SM_EXIT, NULL);
    if (status) sm_fatal("failed to send close to allocator");
    
    /* Wait for an acknowledgement */
    status = sm_recv(sm_nid, &message);
    if (status) sm_fatal("failed to receive closing acknowledgement");
    
    munmap(sm_map, 0xFFFF * getpagesize());
    fflush(stdout);
    return;
}

void *sm_malloc (size_t size) {
    int status = 0, offset = 0;
    msg_t *message;

    /* Send a message to the allocator to allocate some memory */
    char buffer[] = {size};
    status = sm_send(sm_nid, SM_ALOC, buffer);
    if (status) {
        sm_fatal("milk");
        return NULL;
    }

    /* Wait for a reply with the memory allocation offset */
    status = sm_recv_type(sm_sock, &message, SM_ALOC_REPLY);
    if (status) {
        sm_fatal("milk");
        return NULL;
    }

    offset = message->buffer[3];
    mprotect(sm_map + offset, getpagesize(), PROT_READ|PROT_WRITE);
    memset(sm_map+offset, 0, size);

    fflush(stdout);
    return (void *)(sm_map + offset);
}

void sm_barrier (void) {
    int status;
    msg_t *message;

    status = sm_send(sm_nid, SM_BARR, NULL);
    if (status) sm_fatal("milk");

    /* Wait for an acknowledgement */
    status = sm_recv_type(sm_nid, &message, SM_BARR_REPLY);
    if (status || message->type != SM_BARR_REPLY) {
        sm_fatal("failed to receive barrier acknowledgement");
    }

    fflush(stdout);
    return;
}

void sm_bcast (void **addr, int root_nid) {
    int status;
    msg_t *message;

    sm_barrier();

    char buffer[] = {root_nid, (char *) *addr};
    status = sm_send(sm_nid, SM_CAST, buffer);
    if (status) sm_fatal("milk");
    
    /* Wait for an acknowledgement */
    status = sm_recv_type(sm_sock, &message, SM_CAST_REPLY);
    if (status) sm_fatal("failed to receive cast acknowledgement");

    *addr = message->buffer[3];
    fflush(stdout);
    return;
}