#include <stdlib.h>
#include "dsm.h"

#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#define PORT    9243
#define MSG_MAX 128

/* */
typedef struct memory_allocation {
    int offset;     /* The offset of this allocation within it's page */
    int size;       /* The size of this allocation */
} alloc_t;

/* */
typedef struct memory_page {
    int offset;         /* The offset of this page relative to the memory base */
    int allocated;      /* The amount of memory that has been allocated */
    
    int writer;         /* The nid of the node with writer permissions */
    int *reader;        /* All of the nodes with a copy of the page */

    int n_allocs;       /* The number of memory allocations */
    alloc_t **allocs;   /* List of allocated chunks of memory in the page */
} page_t;

/*  */
typedef struct client_message {
    int   nid;      /* The node id of the client making the request */
    char *request;  /* The client's request */
    struct client_message *next;
} msg_t;

/* */
typedef struct allocator_information {
    int        n_nodes;     /* The number of nodes currently running */
    int        total_nodes; /* The total number of nodes to run */
    int        socket;      /* The socket used to receive connections */
    int       *c_sockets;   /* All of the connected client sockets */
    FILE      *log;         /* The log file for the operations performed */
    page_t   **page_list;   /* The pages in the shared memory map */
    msg_t     *m_queue;     /* A queue of messages received from clients */
    msg_t     *m_last;      /* The last message in the queue*/
} allocator_t;

int allocator_init(metadata_t *metadata, allocator_t *allocator);
int allocate      (metadata_t *metadata, allocator_t *allocator);
int allocator_end (allocator_t *allocator);
int fatal         (char *message);

int enqueue(allocator_t *allocator, int nid, char request[]);
int dequeue(allocator_t *allocator);

int node_init    (allocator_t *allocator, int client);
int node_wait    (allocator_t *allocator, int nid, char *message, void **ret, int root);
int node_execute (allocator_t *allocator, int nid, char request[]);
int node_close   (allocator_t *allocator, int nid, char request[]);
int node_barrier (allocator_t *allocator, int nid, char request[]);
int node_allocate(allocator_t *allocator, int nid, char request[]);
int node_cast    (allocator_t *allocator, int nid, char request[]);
int handle_fault (allocator_t *allocator, int nid, char request[]);

#endif