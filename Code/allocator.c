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

int fatal(char *message) {
    fprintf(stderr, "Error: %s.\n", message);
    return -1;
}

int allocate(metadata_t *metadata, allocator_t *allocator) {
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

        activity = select(max_sock+1, &fds, NULL, NULL, NULL);
        /* Read messages coming in from the existing nodes */
        for (int i = 0; i < allocator->total_nodes; i++) {
            if (FD_ISSET(c_sockets[i], &fds)) {
                memset(buffer, 0, 1024);
                recv(c_sockets[i], buffer, 1023, 0);

                /* Add the received message to the queue */
                message_add(allocator, i, buffer);
            }
        }

        /* Execute all of the messages in the queue */
        status = node_execute(allocator);
        if (status) return fatal("Failed to execute command");
    }

    allocator_end(allocator);
    while(wait(NULL) > 0);
    return 0;
}

int node_init(allocator_t *allocator, int client) {
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

/*
 * The execution of command was extracted into a separate function for reuse
 * the barrier function
*/
int node_execute(allocator_t *allocator) {
    int status = -1;
    request_t *request = allocator->m_queue;

    /* Go through each message in the queue and execute it */
    while (request) {
        fprintf(stderr, "---> command: '%s'\n", request->command);

        /* Handle sm_node_exit() */
        if (strstr(request->command, "close")) {
            status = node_close(allocator, request);
        /* Handle sm_barrier() */
        } else if (strstr(request->command, "barrier")) {
            status = node_barrier(allocator, request);
        /* Handle sm_malloc() */
        } else if (strstr(request->command, "allocate")) {
            status = node_allocate(allocator, request);
        /* Handle sm_bcast() */
        } else if (strstr(request->command, "cast")) {
            status = node_cast(allocator, request);
        /* Handle a signal */
        } else if (strstr(request->command, "handle")) {
            status = handle_fault(allocator, request);
        /* Handle an invalid command received */
        } else {
            fprintf(stderr, "===> Failed command: '%s'\n", request->command);
            status = fatal("Invalid message received");
        }

        request = request->next;
        message_rm(allocator, NULL);

        if (status) return status;
    }

    return status;
}

int node_close(allocator_t *allocator, request_t *request) {
    int nid, *c_sockets;
    char buffer[1024];

    nid = request->nid, c_sockets = allocator->c_sockets;

    /* Return a message to the node, indicating it has been closed */
    snprintf(buffer, MSG_LEN_MAX, "close ACK");
    send(c_sockets[nid], buffer, strlen(buffer), 0);

    /* Close and NULL out the clients socket from the list */
    close(c_sockets[nid]);
    c_sockets[nid] = 0;
    allocator->n_nodes--;

    return 0;
}

/* 
 * Wait for each node to have sent the correct message (either barrier() or bm_cast())
 * 
 * First checks the message queue for already received messages, then receives messages
 * from each node until they send the correct message, enqueuing other messages received
*/
int node_wait(allocator_t *allocator, request_t *request, char *message, void **ret, int root) {
    int c_nid = request->nid, recvd = 0, mode, *c_sockets = allocator->c_sockets;
    char buffer[1024];

    /* Differentiate between barrier and cast requests by the message received */
    mode = (strcmp(message, "barrier")) ? 1 : 0;

    for (int i = 0; i < allocator->total_nodes; i++) {
        /* Don't check the node that initiated the request */
        if (i == c_nid) continue;

        recvd = 0;
        /* Check whether the message is in the message queue already */
        for (request_t *queue = allocator->m_queue; queue; queue = queue->next) {
            if (strstr(queue->command, message) && (i == queue->nid)) {
                /* If it is a cast request, capture the address from the root node */
                if (i == root && mode == 1) 
                    sscanf(queue->command, "cast %p nid %*d root %*d", ret);
                message_rm(allocator, queue);
                recvd = 1;
                break;
            }
        }

        if (recvd) continue;

        /* 
         * Otherwise receive requests from the node until the message is received
         * enqueueing all other messages
         */
        while(1) {
            memset(buffer, 0, 1024);
            recv(c_sockets[i], buffer, 1023, 0);

            /* If the request is a barrier, go to the next node */
            if (strstr(buffer, message)) {
                /* If it is a cast request, capture the address from the root node */
                if (i == root && mode == 1)
                    sscanf(buffer, "cast %p nid %*d root %*d", ret);
                break;
            }

            /* If the request isn't correct, enqueue it */
            message_add(allocator, i, buffer);
        }
    }

    return 0;
}

int node_barrier(allocator_t *allocator, request_t *request) {
    char buffer[1024] = "\0";
    int *c_sockets = allocator->c_sockets, status = 0;

    /*
     * The functionality to wait for all of the nodes to send a message
     * before continuing was extracted into a separate function to be reused for sm_bcast
    */
    status = node_wait(allocator, request, "barrier", NULL, 0);
    if (status) return fatal("node wait failed");

    /* Once all of the nodes have completed the barrier, send them a ACK */
    for (int i = 0; i < allocator->total_nodes; i++) {
        snprintf(buffer, 1024, "barrier ACK");
        send(c_sockets[i], buffer, strlen(buffer), 0);
    }

    return 0;
}

int node_allocate(allocator_t *allocator, request_t *request) {
    int nid, offset, client = allocator->c_sockets[request->nid];
    char buffer[1024] = "\0";
    size_t size;
    alloc_t *allocation = malloc(sizeof(alloc_t));
    page_t *page;

    /* Find which node is allocating, and how much memory they need */
    sscanf(request->command, "allocate node %d size %ld", &nid, &size);

    /* Find a memory page for the allocation to be stored on */
    for (int i = 0; i < 0xFFFF; i++) {
        page = allocator->page_list[i];

        /* Check the page has enough unallocated memory to satisfy the request */
        if ((page->allocated + size) <= getpagesize()) {
            /* Create the memory allocation data */
            allocation->writer     = nid;
            allocation->readers    = malloc(allocator->total_nodes * sizeof(int));
            allocation->readers[0] = nid;
            allocation->readers[1] = -1;
            allocation->size       = size;
            allocation->offset     = page->allocated;

            page->allocated += allocation->size;
            break;
        }
    }

    snprintf(buffer, 0x20, "offset %d", page->offset + allocation->offset);
    send(client, buffer, strlen(buffer), 0);

    return 0;
}

int node_cast(allocator_t *allocator, request_t *request) {
    void *address;
    char buffer[1024];
    int nid, root, *c_sockets = allocator->c_sockets, status = 0;

    /* Read the message to find the node the correct address is stored in */
    sscanf(request->command, "cast %p nid %d root %d", &address, &nid, &root);

    /*
     * The functionality to wait for all of the nodes to send a message
     * before continuing was extracted into a separate function to be reused for sm_barrier
    */
    status = node_wait(allocator, request, "cast", &address, root);
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

int handle_fault(allocator_t *allocator, request_t *request) {
    int nid, offset, page_offset, alloc_offset, page_owner, value;
    char buffer[1024];
    alloc_t *allocation;

    /* The offset within the page*/
    alloc_offset = offset % getpagesize();
    /* The page number */
    page_offset  = (offset - alloc_offset) / getpagesize();
    /* Get the allocation from the page list */
    allocation = allocator->page_list[page_offset]->allocs[alloc_offset];

    /* Find who has write permissions for the page (most up-to-date value) */
    page_owner = allocation->writer;

    /* Message the owner of the chunk and request it's value */
    snprintf(buffer, 1023, "request %d", offset);
    send(allocator->c_sockets[page_owner], buffer, strlen(buffer), 0);
    
    /* Receive the value from the owner */
    recv(allocator->c_sockets[page_owner], buffer, 1023, 0);
    
    /* Send this to the node that triggered the fault */
    send(allocator->c_sockets[nid], buffer, strlen(buffer), 0);

    return 0;
}




int message_add(allocator_t *allocator, int nid, char buffer[]) {
    request_t *new, *last;

    /* Create the new message */
    new = malloc(sizeof(request_t));
    new->nid     = nid;
    new->command = strndup(buffer, MSG_LEN_MAX);
    new->next    = NULL;
    new->prev    = NULL;

    /* Create the new queue if no messages */
    if (allocator->m_queue == NULL) {
        allocator->m_queue = new;
        allocator->m_last  = new;
    /* Otherwise add it to the queue */
    } else {
        last = allocator->m_last;
        last->next = new;
        new->prev  = last;
        allocator->m_last  = new;
    }
    
    return 0;
}

/* 
 * Remove the given message from the message queue, if the 
 * provided message is NULL, remove from the front of the queue
 */
int message_rm(allocator_t *allocator, request_t *request) {
    /* Remove a request from the middle of the queue (used for barriers/casts) */
    if (request) {
        if (request->prev) request->prev->next = request->next;
        if (request->next) request->next->prev = request->prev;
        if (allocator->m_queue == request) allocator->m_queue = request->next;
    /* Remove from the front of the queue */
    } else {
        request = allocator->m_queue;
        allocator->m_queue = request->next;
        if (allocator->m_queue) {
            allocator->m_queue->prev = NULL;
        }
    }

    if (allocator->m_queue == NULL) allocator->m_last = NULL;

    free(request->command);
    free(request);

    return 0; 
}

int allocator_init(metadata_t *metadata, allocator_t *allocator) {
    struct sockaddr_in address;
    int status, sock, opt = 1;

    allocator->n_nodes = 0;
    allocator->total_nodes = metadata->n_proc;

    /* Initialize all of the pages to unused */
    page_t **page_list = malloc(0xFFFF * sizeof(page_t *));
    for (int i = 0; i < 0xFFFF; i++) {
        page_list[i] = malloc(sizeof(page_t));
        page_list[i]->mapped    = malloc(allocator->total_nodes * sizeof(int));
        page_list[i]->mapped[0] = -1;
        page_list[i]->offset    = i * getpagesize();
        page_list[i]->allocated = 0;
        page_list[i]->n_allocs  = 0;
        page_list[i]->allocs    = malloc(sizeof(alloc_t *));
    }
    allocator->page_list = page_list;

    /* Initialize all the client sockets to 0 */
    allocator->c_sockets = malloc(allocator->total_nodes * sizeof(int));
    for (int i = 0; i < allocator->total_nodes; i++)
        allocator->c_sockets[i] = 0;

    /* Create the message queue for the sockets requests */
    allocator->m_queue = NULL;
    allocator->m_last  = NULL;

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

int allocator_end(allocator_t *allocator) {
    page_t **page_list = allocator->page_list;

    /* Free the page list  */
    for (int i = 0; i < 0xFFFF; i++) {
        for (int j = 0; j < page_list[i]->n_allocs; j++) {
            free(page_list[i]->allocs[j]->readers);
            free(page_list[i]->allocs[j]);
        }
        free(page_list[i]->allocs);
        free(page_list[i]->mapped);
        free(page_list[i]);
    }
    free(page_list);

    /* Free the list of client sockets */
    free(allocator->c_sockets);

    close(allocator->socket);

    return 0;
}