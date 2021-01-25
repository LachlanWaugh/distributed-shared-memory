#define _GNU_SOURCE

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
#include <ucontext.h>
#include <errno.h>

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
    /* Find the offset of the variable from the memory base */
    long offset = (char *) si->si_addr - sm_map;

    /* Determine if it is a read or write fault, and direct it to the relevant function */
    if (((ucontext_t *)ctx)->uc_mcontext.gregs[REG_ERR] & 0x2) {
        sm_write_fault(si, offset);
    } else {
        sm_read_fault(si, offset);
    }

    return;
}

int sm_read_fault(siginfo_t *si, long offset) {
    char buffer[4096] = "\0";
    int status, size, value;
    msg_t *message;
    
    /* Send a message to the allocator to find the value at the address */
    snprintf(buffer, sizeof(long), "%ld", offset);
    status = sm_send(sm_sock, sm_nid, SM_READ, buffer);
    if (status) sm_fatal("milk");

    /* Wait for a response containing the new page */
    status = sm_recv_type(sm_sock, &message, SM_READ_REPLY);
    if (status) sm_fatal("failed to receive read fault ACK");
    sscanf(message->buffer, "%d %d", &size, &value);

    /* Write the new value to the page */
    mprotect(si->si_addr, size, PROT_READ);
    *(char *)(si->si_addr) = value;

    return 0;
}

int sm_write_fault(siginfo_t *si, long offset) {
    char buffer[4096] = "\0";
    int status, size;
    msg_t *message;
    
    /* Send a message to the allocator to find the value at the address */
    snprintf(buffer, 4096, "%ld", offset);
    status = sm_send(sm_sock, sm_nid, SM_WRIT, buffer);
    if (status) sm_fatal("milk");

    /* Wait for a response containing the new page */
    status = sm_recv_type(sm_sock, &message, SM_WRIT_REPLY);
    if (status) sm_fatal("failed to receive read fault ACK");

    /* Write the new value to the page */
    mprotect(si->si_addr, getpagesize(), PROT_WRITE | PROT_READ);
    *(char *)(si->si_addr) = 0; // TODO: Change to the relevant value

    return 0;
}

void sm_poll(int signum) {
    char buffer[4196] = "\0";
    int offset, status;
    msg_t *message;

    status = sm_recv(sm_sock, &message);
    if (status) sm_fatal("failed to receive read request");

    sscanf(buffer, "%d", &offset);

    /* Handle a read request for a memory address */
    if (message->type == SM_REQUEST) {
        /* Send the request page back */
        snprintf(buffer, 4096 + 6, "%s", sm_map + offset);
        status = sm_send(sm_sock, sm_nid, SM_REQU_REPLY, buffer);
        if (status) sm_fatal("failed to send page to allocator");
    /* Handle a loss of write permissions */
    } else if (message->type == SM_RELEASE) {
        /* Invalidate the required memory and send an acknowledgement */
        mprotect(sm_map + offset, getpagesize(), PROT_NONE);

        status = sm_send(sm_sock, sm_nid, SM_RLSE_REPLY, NULL);
        if (status) sm_fatal("failed to send invalidation acknowledgement to allocator");
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

    return 0;
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
    status = sm_send(sm_sock, sm_nid, SM_INIT, NULL);
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
    status = sm_send(sm_sock, sm_nid, SM_EXIT, NULL);
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
    status = sm_send(sm_sock, sm_nid, SM_ALOC, buffer);
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

    status = sm_send(sm_sock, sm_nid, SM_BARR, NULL);
    if (status) sm_fatal("milk");

    /* Wait for an acknowledgement */
    status = sm_recv_type(sm_sock, &message, SM_BARR_REPLY);
    if (status || message->type != SM_BARR_REPLY) {
        sm_fatal("failed to receive barrier acknowledgement");
    }

    fflush(stdout);
    return;
}

void sm_bcast (void **addr, int root_nid) {
    int status;
    char buffer[1024];
    msg_t *message;

    sm_barrier();

    snprintf(buffer, 1024, "%d %s", root_nid, (char *) *addr);
    status = sm_send(sm_sock, sm_nid, SM_CAST, buffer);
    if (status) sm_fatal("milk");
    
    /* Wait for an acknowledgement */
    status = sm_recv_type(sm_sock, &message, SM_CAST_REPLY);
    if (status) sm_fatal("failed to receive cast acknowledgement");

    sscanf(message->buffer, "%*d %*d %*d %s", (char *) *addr);
    
    fflush(stdout);
    return;
}