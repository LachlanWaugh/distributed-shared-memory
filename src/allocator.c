#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "allocator.h"
#include "config.h"

int sm_fatal(char *message) {
    fprintf(stderr, ANSI_COLOR_RED "Error: %s.\n" ANSI_COLOR_RESET, message);
    return -1;
}

int allocator_init() {
    struct sockaddr_in address;
    int status, sock, opt = 1;

    /* Create and initialize the list of memory pages */
    for (int i = 0; i < SM_MAX_PAGES; i++) {
        sm_page_table[i].writer = -1;
        sm_page_table[i].readers = malloc(options->n_nodes * sizeof(int));

        for (int j = 0; j < options->n_nodes; j++) {
            sm_page_table[i].readers[j] = 0;
        }
    }

    /* Initialize all the client sockets to 0 */
    client_sockets = malloc(options->n_nodes * sizeof(int));
    for (int i = 0; i < options->n_nodes; i++) {
        client_sockets[i] = 0;
    }

    /*  */
    if (options->log_file) {
        fprintf(options->log_file, "-= %d node processes\n", options->n_nodes);
    } else {
        options->log_file = NULL;
    }

    /* Prepare the shared memory mapping and information */
    /* Keep a cache of the memory map in the allocator to reduce the overhead of read-faults */
    sm_memory_map = mmap((void *)0x6f0000000000, 0xFFFF * getpagesize(), 
                PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (sm_memory_map == MAP_FAILED) return sm_fatal("failed to map memory");
    sm_node_count = 0;

    /* Create the communication socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) return sm_fatal("failed to create socket");
    sm_socket = sock;

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(SM_PORT);

    status = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if (status < 0) return sm_fatal("failed setting socket options");

    status = bind(sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) return sm_fatal("failed to bind socket");

    status = listen(sock, options->n_nodes);
    if (status < 0) return sm_fatal("failed to listen to socket");

    return 0;
}

int allocator_end() {
    /* Free the page list  */
    for (int i = 0; i < SM_MAX_PAGES; i++) {
        free(sm_page_table[i].writers);
    }

    /* Free the list of client sockets */
    free(client_sockets);

    if (options->log_file)
        fclose(options->log_file);
    
    close(sm_socket);

    return 0;
}

int allocate() {
    struct sockaddr_in address;
    fd_set fds;
    int status, max_sock, activity, client, addrlen = sizeof(address),
    char buffer[1024] = "\0";

    /* First, initialize all of the client nodes' sockets */
    while (sm_node_count < options->n_nodes) {
        /* Check the socket for the new connection */
        client = accept(sm_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client < 0) {
            return sm_fatal("Failed to accept connections");
        }

        /* Initailize the new node and socket */
        status = node_init(allocator, client);
        if (status < 0) {
            return sm_fatal("Node initialization failed");
        }
    }

    /*
     * Wait for messages from the clients to come in, running until all nodes have been closed
    */
    while(sm_node_count > 0) {
        FD_ZERO(&fds);
        max_sock = 0;

        /* Initialize the list of client sockets */
        for (int i = 0; i < options->n_nodes; i++) {
            if (client_sockets[i] > 0)
                FD_SET(client_sockets[i], &fds);

            if (client_sockets[i] > max_sock)
                max_sock = client_sockets[i];
        }

        activity = select(max_sock+1, &fds, NULL, NULL, NULL);
        /* Check each client to see if they have any pending requests */
        for (int i = 0; i < options->n_nodes; i++) {
            if (FD_ISSET(client_sockets[i], &fds)) {
                memset(buffer, 0, 1024);
                recv(client_sockets[i], buffer, 1023, 0);

                /* Execute the received request */
                status = node_execute(i, buffer);
                if (status) {
                    return sm_fatal("failed to execute command");
                }
            }
        }
    }

    allocator_end();
    while(wait(NULL) > 0);
    return 0;
}

/* Initialize the connection between the allocator and the client node */
int node_init(int client) {
    /* Ensure that it is an initialization request */
    msg_t *init = sm_recv(client);
    if (init->type != SM_INIT) {
        return sm_fatal("invalid initialization request");
    }

    /* */
    sm_send(client, SM_INIT_ACK, NULL);

    /* Add it to the database */
    client_sockets[sm_node_count] = client;
    sm_node_count++;

    return 0;
}

/* Remove the memory allocated to a node and close it's socket */
int node_close(int nid, char request[]) {
    /* */
    sm_send(client_sockets[nid], SM_EXIT-_ACK, NULL);

    /* Close and NULL out the clients socket from the list */
    close(client_sockets[nid]);
    client_sockets[nid] = 0;
    allocator->n_nodes--;

    return 0;
}

/* Pass the received command from the client to the correct function to execute it */
int node_execute(msg_t *request) {
    switch(request->type) {
        case SM_EXIT: /* Handle sm_node_exit() */
            return node_close(request->nid, request->buffer);
        case SM_BARR: /* Handle sm_barrier() */
            return node_barrier(request->nid, request->buffer);
        case SM_ALOC: /* Handle sm_malloc() */
            return node_allocate(request->nid, request->buffer);
        case SM_CAST: /* Handle sm_bcast() */
            return node_cast(request->nid, request->buffer);
        case SM_READ: /* Handle a read fault */
            return handle_fault(request->nid, request->buffer);
        case SM_WRIT: /* Handle a write fault */
            return handle_fault(request->nid, request->buffer);
        default: /* Handle an invalid command received */
            return sm_fatal("Invalid message received");
    }
}

/* Wait for each node to have sent the correct message (either barrier() or bm_cast()) */
int node_wait(int nid, int mode, void **ret, int root) {
    int status;
    char buffer[1024];

    for (int i = 0; i < options->n_nodes; i++) {
        /* Don't check the node that initiated the request */
        if (i == nid) continue;

        /* receive and execute requests until the correct message is found (barrier/cast) */
        while(1) {
            memset(buffer, 0, 1024);
            status = sm_recv(client_sockets[i]);
            if (status <= 0) sm_fatal("wait: failed to receive message from socket");

            /* If the request is a barrier, go to the next node */
            if (strstr(buffer, message)) {
                /* If it is a cast request, capture the address from the root node */
                if (i == root && mode == 1) sscanf(buffer, "cast %p nid %*d root %*d", ret);
                break;
            }

            /* If the request isn't correct, execute it */
            status = node_execute(allocator, i, buffer);
            if (status) return sm_fatal("failed to exit command");
        }
    }

    return 0;
}

/* */
int node_barrier(int nid, char request[]) {
    char buffer[1024] = "\0";
    int status = 0;

    /*
     * The functionality to wait for all of the nodes to send a message
     * before continuing was extracted into a separate function to be reused for sm_bcast
    */
    status = node_wait(nid, SM_BARR, NULL, 0);
    if (status) {
        return sm_fatal("node wait failed");
    }

    /* Once all of the nodes have completed the barrier, send them a ACK */
    for (int i = 0; i < options->n_nodes; i++) {
        sm_send(client_socket[i], SM_BARR_ACK);
    }

    return 0;
}

/* Allocate some memory for the node and store metadata about it */
int node_allocate(int nid, char request[]) {
    int offset, client = allocator->c_sockets[nid];
    char buffer[1024] = "\0";
    size_t size;
    alloc_t *alloc = malloc(sizeof(alloc_t));
    page_t  *page;

    /* Find how much memory the node is requiring */
    sscanf(request, "allocate node %*d size %ld", &size);

    /* Find a memory page for the allocation to be stored on */
    for (int i = 0; i < 0xFFFF; i++) {
        page = allocator->page_list[i];

        /* Check the page has enough unallocated memory to satisfy the request */
        if ((page->allocated + size) <= getpagesize()) {
            /* Create the memory allocation data */
            page->writer      = nid;
            page->reader[nid] = 1;

            alloc->size       = size;
            alloc->offset     = page->allocated;

            page->allocated += alloc->size;
            page->allocs[page->n_allocs++] = alloc;
            break;
        }
    }

    /* Return a message informing the client of the offset their allocation will be at */
    snprintf(buffer, 0x20, "offset %d", page->offset + alloc->offset);
    send(client, buffer, strlen(buffer), 0);

    /* Write the action to the log file */
    if (allocator->log)
        fprintf(allocator->log, "#%d: allocated %d-%d\n", nid, page->offset, alloc->offset);

    return 0;
}

int node_cast(int nid, char request[]) {
    void *address;
    char buffer[1024];
    int root, status, *c_sockets = allocator->c_sockets;

    /* Read the message to find the node the correct address is stored in */
    sscanf(request, "cast %p nid %*d root %d", &address, &root);

    /*
     * The functionality to wait for all of the nodes to send a message
     * before continuing was extracted into a separate function to be reused for sm_barrier
    */
    status = node_wait(allocator, nid, "cast", &address, root);
    if (status) return sm_fatal("node wait failed");

    /* All of the nodes have hit the cast, so send back the new value */
    for (int i = 0; i < allocator->total_nodes; i++) {
        memset(buffer, 0, 1024);

        /* The root_nid node also will be waiting on an acknowledgement */
        snprintf(buffer, 1024, "address %p", address);
        send(c_sockets[i], buffer, strlen(buffer), 0);
    }

    return 0;
}

int handle_fault(int nid, char request[]) {
    int offset, type, page_n, status;
    char buffer[4196] = "\0", type_s[100] = "\0";
    page_t *page;

    sscanf(request, "%s fault: node %*d offset %d", type_s, &offset);
    /* Determine if it's a read or write fault */
    type = (strstr(type_s, "read")) ? 1: 0;

    page_n = offset / getpagesize();
    /* Get the allocation from the page list */
    page = allocator->page_list[page_n];

    /* Message the owner of the chunk and request it's page */
    snprintf(buffer, 1023, "request %s: %d", type_s, page->offset);
    status = send(allocator->c_sockets[page->writer], buffer, strlen(buffer), 0);
    if (status <= 0) return sm_fatal("failed to send page request to node");
    
    if (allocator->log) {
        if (type == 1) {
            fprintf(allocator->log, "#%d: read fault @ %d\n\
                                     #%d: releasing ownership of %d\n\
                                     #%d: receiving read permission for %d\n", 
                                    nid, page_n, page->writer, page_n, nid, page_n);
        } else {
            fprintf(allocator->log, "#%d: write fault @ %d\n\
                                     #%d: releasing ownership of %d\n\
                                     #%d: receiving ownership of %d\n", 
                                    nid, page_n, page->writer, page_n, nid, page_n);
        }
    }

    /* Receive messages from the page owner until the page is found, enqueueing other messages */
    while (1) {
        /* Receive the page from the page's owner */
        memset(buffer, 0, 4196);
        status = recv(allocator->c_sockets[page->writer], buffer, 4196, 0);
        if (status <= 0) return sm_fatal("failed to receive message in fault handler");

        /* Enqueue messages that aren't the page */
        if (strstr(buffer, "page: ") == 0) {
            enqueue(allocator, page->writer, buffer);
        /* Otherwise capture the page and return it */
        } else {
            break;
        }
    }

    /* Send this to the node that triggered the fault */
    status = send(allocator->c_sockets[nid], buffer, strlen(buffer), 0);
    if (status <= 0) return sm_fatal("failed to send page to node");

    return 0;
}