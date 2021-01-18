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

#include "allocator.h"
#include "dsm.h"

int allocate() {
    struct sockaddr_in address;
    fd_set fds;
    int status, max_sock, activity, client, addrlen = sizeof(address),
        *c_sockets = allocator->c_sockets;
    char buffer[1024] = "\0";

    /* First, initialize all of the client nodes' sockets */
    while (allocator->n_nodes < allocator->total_nodes) {
        /* Check the socket for the new connection */
        client = accept(allocator->socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client < 0) return fatal("Failed to accept connections");

        /* Initailize the new node and socket */
        status = node_init(allocator, client);
        if (status < 0) return fatal("Node initialization failed");
    }

    /*
     * Wait for messages from the clients to come in, running 
     * until all nodes have been closed
    */
    while(allocator->n_nodes > 0) {
        FD_ZERO(&fds);
        max_sock = 0;

        /* Initialize the list of client sockets */
        for (int i = 0; i < allocator->total_nodes; i++) {
            if (c_sockets[i] > 0)        FD_SET(c_sockets[i], &fds);
            if (c_sockets[i] > max_sock) max_sock = c_sockets[i];
        }

        /*
         * Check the message queue to make sure there aren't any pending messages 
         * this is used in the fault handler to receive the value of a page without losing
         * messages from clients that have already been sent
        */
        for (msg_t *msg = allocator->m_queue; allocator->m_queue; dequeue(allocator)) {
            status = node_execute(allocator, msg->nid, msg->request);
            if (status) return fatal("failed to execute command");
        }

        activity = select(max_sock+1, &fds, NULL, NULL, NULL);
        /* Check each client to see if they have any pending requests */
        for (int i = 0; i < allocator->total_nodes; i++) {
            if (FD_ISSET(c_sockets[i], &fds)) {
                memset(buffer, 0, 1024);
                recv(c_sockets[i], buffer, 1023, 0);

                /* Execute the received request */
                status = node_execute(allocator, i, buffer);
                if (status) return fatal("failed to execute command");
            }
        }
    }

    allocator_end(allocator);
    while(wait(NULL) > 0);
    return 0;
}

/* Initialize the connection between the allocator and the client node */
int node_init(int client) {
    char buffer[1024];

    /* Ensure that it is an initialization request */
    recv(client, buffer, 1023, 0);
    if (strcmp(buffer, "init")) 
        return fatal("invalid initialization request");

    /* Add it to the database */
    allocator->c_sockets[allocator->n_nodes] = client;
    allocator->n_nodes++;

    /* Return a message to the node, indicating n_nodes and it's nid */
    snprintf(buffer, MSG_LEN_MAX, "nid: %d, nodes: %d\n", 
                allocator->n_nodes - 1, allocator->n_nodes);
    send(client, buffer, strlen(buffer), 0);

    return 0;
}

/* Pass the received command from the client to the correct function to execute it */
int node_execute(int nid, char request[]) {
    int status = -1;

    /* Handle sm_node_exit() */
    if (strstr(request, "close")) {
        status = node_close(allocator, nid, request);
    /* Handle sm_barrier() */
    } else if (strstr(request, "barrier")) {
        status = node_barrier(allocator, nid, request);
    /* Handle sm_malloc() */
    } else if (strstr(request, "allocate")) {
        status = node_allocate(allocator, nid, request);
    /* Handle sm_bcast() */
    } else if (strstr(request, "cast")) {
        status = node_cast(allocator, nid, request);
    /* Handle a signal */
    } else if (strstr(request, "fault")) {
        status = handle_fault(allocator, nid, request);
    /* Handle an invalid command received */
    } else {
        status = fatal("Invalid message received");
    }

    return status;
}

/* Remove the memory allocated to a node and close it's socket */
int node_close(int nid, char request[]) {
    int *c_sockets = allocator->c_sockets;
    char buffer[1024];

    /* Return a message to the node, indicating it has been closed */
    snprintf(buffer, MSG_LEN_MAX, "close ACK");
    send(c_sockets[nid], buffer, strlen(buffer), 0);

    /* Close and NULL out the clients socket from the list */
    close(c_sockets[nid]);
    c_sockets[nid] = 0;
    allocator->n_nodes--;

    return 0;
}

/* Wait for each node to have sent the correct message (either barrier() or bm_cast() */
int node_wait(int nid, char *message, void **ret, int root) {
    int mode, *c_sockets = allocator->c_sockets, status;
    char buffer[1024];

    /* Differentiate between barrier and cast requests by the message received */
    mode = (strcmp(message, "barrier")) ? 1 : 0;

    for (int i = 0; i < allocator->total_nodes; i++) {
        /* Don't check the node that initiated the request */
        if (i == nid) continue;

        /* receive and execute requests until the correct message is found (barrier/cast) */
        while(1) {
            memset(buffer, 0, 1024);
            status = recv(c_sockets[i], buffer, 1023, 0);
            if (status <= 0) fatal("wait: failed to receive message from socket");

            /* If the request is a barrier, go to the next node */
            if (strstr(buffer, message)) {
                /* If it is a cast request, capture the address from the root node */
                if (i == root && mode == 1) sscanf(buffer, "cast %p nid %*d root %*d", ret);
                break;
            }

            /* If the request isn't correct, execute it */
            status = node_execute(allocator, i, buffer);
            if (status) return fatal("failed to exit command");
        }
    }

    return 0;
}

int node_barrier(int nid, char request[]) {
    char buffer[1024] = "\0";
    int *c_sockets = allocator->c_sockets, status = 0;

    /*
     * The functionality to wait for all of the nodes to send a message
     * before continuing was extracted into a separate function to be reused for sm_bcast
    */
    status = node_wait(allocator, nid, "barrier", NULL, 0);
    if (status) return fatal("node wait failed");

    /* Once all of the nodes have completed the barrier, send them a ACK */
    for (int i = 0; i < allocator->total_nodes; i++) {
        snprintf(buffer, 1024, "barrier ACK");
        send(c_sockets[i], buffer, strlen(buffer), 0);
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
    if (status) return fatal("node wait failed");

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
    if (status <= 0) return fatal("failed to send page request to node");
    
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
        if (status <= 0) return fatal("failed to receive message in fault handler");

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
    if (status <= 0) return fatal("failed to send page to node");

    return 0;
}

int allocator_init() {
    struct sockaddr_in address;
    int status, sock, opt = 1;

    /* */
    for (int i = 0; i < SM_MAX_PAGES) {
        sm_page_table[i].writer = -1;
        sm_page_table[i].readers = malloc(options->n_nodes * sizeof(int));

        for (int j = 0; j < options->n_nodes) {
            sm_page_table[i].readers[j] = 0;
        }
    }

    /* Initialize all the client sockets to 0 */
    client_sockets = malloc(options->n_nodes * sizeof(int));
    for (int i = 0; i < options->n_nodes; i++) {
        client_sockets[i] = 0;
    }

    /* Create/initiailze the log file */
    if (options->log_file) {
        fprintf(allocator->log, "-= %d node processes\n", allocator->total_nodes);
    } else {
        allocator->log = NULL;
    }

    /* Create the communication socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) return fatal("failed to create socket");
    allocator->socket = sock;

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    status = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if (status < 0) return fatal("failed setting socket options");

    status = bind(sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) return fatal("failed to bind socket");

    status = listen(sock, metadata->n_proc);
    if (status < 0) return fatal("failed to listen to socket");

    return 0;
}

int allocator_end() {
    page_t **page_list = allocator->page_list;

    /* Free the page list  */
    for (int i = 0; i < 0xFFFF; i++) {
        for (int j = 0; j < page_list[i]->n_allocs; j++)
            free(page_list[i]->allocs[j]);
        free(page_list[i]->allocs);
        free(page_list[i]);
    }
    free(page_list);

    /* Free the list of client sockets */
    free(allocator->c_sockets);

    if (allocator->log) fclose(allocator->log);
    close(allocator->socket);

    return 0;
}