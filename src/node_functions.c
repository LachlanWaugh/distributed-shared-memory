#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "node_functions.h"
#include "allocator.h"
#include "config.h"

/* Initialize the connection between the allocator and the client node */
int node_init(int client) {
    msg_t *init = NULL;

    /* Add it to the database */
    client_sockets[sm_node_count] = client;

    /* Ensure that it is an initialization request */
    int status = sm_recv(sm_node_count, &init);
    if (status || init->type != SM_INIT) {
        return sm_fatal("invalid initialization request");
    }

    /* */
    sm_send(sm_node_count, SM_INIT_REPLY, NULL);
    sm_node_count++;

    return 0;
}

/* Remove the memory allocated to a node and close it's socket */
int node_close(int nid) {
    /* */
    sm_send(nid, SM_EXIT_REPLY, NULL);

    /* Close and NULL out the clients socket from the list */
    close(client_sockets[nid]);

    client_sockets[nid] = 0;
    sm_node_count--;

    return 0;
}

/* Pass the received command from the client to the correct function to execute it */
int node_execute(msg_t *request) {
    int status = 0;
    
    switch(request->type) {
        case SM_EXIT: /* Handle sm_node_exit() */
            status = node_close(request->nid);
            break;
        case SM_BARR: /* Handle sm_barrier() */
            status = node_barrier(request->nid);
            break;
        case SM_ALOC: /* Handle sm_malloc() */
            status = node_allocate(request->nid, request->buffer);
            break;
        case SM_CAST: /* Handle sm_bcast() */
            status = node_cast(request->nid, request->buffer);
            break;
        case SM_READ: /* Handle a read fault */
        case SM_WRIT: /* Handle a write fault */
            status = handle_fault(request->nid, request->buffer);
            break;
        default: /* Handle an invalid command received */
            status = sm_fatal("Invalid message received");
    }

    free(request);
    return status;
}

/* Wait for each node to have sent the correct message (either barrier() or bm_cast()) */
int node_wait(int nid, int mode, void **ret, int root) {
    int status;
    msg_t *request;

    for (int i = 0; i < options->n_nodes; i++) {
        /* Don't check the node that initiated the request */
        if (i == nid) continue;

        /* receive and execute requests until the correct message is found (barrier/cast) */
        while(1) {
            status = sm_recv(i, &request);
            if (status) sm_fatal("wait: failed to receive message from socket");

            /* If the request is a barrier, and this is a barrier ack go to the next node */
            if (mode == SM_BARR && request->type == SM_BARR_REPLY) {
            /* If the request is a cast and this is a cast ack, capture the value from the root node */
            } else if (mode == SM_CAST && request->type == SM_CAST_REPLY) {
                if (i == root) sscanf(request->buffer, "%p", ret);
            /* Otherwise it is another request, execute it */
            } else {
                status = node_execute(request);
                if (status) return sm_fatal("failed to execute command");
            }

            free(request);
        }
    }

    return 0;
}

/* */
int node_barrier(int nid) {
    int status = 0;

    /* Wait until all nodes have sent a barrier request */
    status = node_wait(nid, SM_BARR, NULL, -1);
    if (status) return sm_fatal("node wait failed");

    /* Once all of the nodes have completed the barrier, send them a ACK */
    for (int i = 0; i < options->n_nodes; i++) {
        sm_send(i, SM_BARR_REPLY, NULL);
    }

    return 0;
}

/* Allocate some memory for the node and store metadata about it */
int node_allocate(int nid, char request[]) {
    int alloc_size, offset;

    /* If there are no free pages, send back a message and return */
    if (sm_current_page >= SM_MAX_PAGES) {
        // send message
        // return
    /* Otherwise allocated the new page for the client sending the request */
    } else {
        /* Find how much memory the node is requesting */
        alloc_size = request[0];

        sm_page_table[sm_current_page]->writer = nid;
        sm_page_table[sm_current_page]->readers[nid] = 1;

        offset = (sm_current_page * SM_PAGE_SIZE) + sm_current_offset;
    }

    /* Return a message informing the client of the offset their allocation will be at */
    char buffer[] = {offset};
    status = sm_send(nid, SM_ALOC_REPLY, buffer);
    if (status) return sm_fatal("milk");

    /* Write the action to the log file */
    if (options->log_file != NULL) {
        fprintf(options->log_file, "#%d: allocated %d-%d\n", nid, sm_current_page, sm_current_offset);
    }

    /* Update the global sizes */
    sm_current_page += alloc_size / SM_PAGESIZE;
    sm_current_offset = (sm_current_offset + alloc_size) % SM_PAGESIZE;

    return 0;
}

int node_cast(int nid, char request[]) {
    int status, root, value;

    root  = request[0];
    value = request[1];

    status = node_wait(nid, SM_CAST, &address, root);
    if (status) return sm_fatal("node wait failed");

    /* All of the nodes have hit the cast, so send back the new value */
    for (int i = 0; i < allocator->total_nodes; i++) {
        memset(buffer, 0, SM_BUFF_SIZE);

        /* The root_nid node also will be waiting on an acknowledgement */
        snprintf(buffer, SM_BUFF_SIZE, "address %p", address);
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