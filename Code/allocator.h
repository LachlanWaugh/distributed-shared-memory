#include <stdlib.h>
#include "dsm.h"

#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#define PORT    9243
#define MSG_MAX 128

/* */
typedef struct memory_allocation {
    int writer;     /* The nid of the node with write permissions */
    int *readers;   /* The nids of the nodes with read permissions */
    int offset;     /* The offset of this allocation within it's page */
    int size;       /* The size of this allocation */
} alloc_t;

/* */
typedef struct memory_page {
    int offset;        /* The offset of this page relative to the memory base */
    int *mapped;       /* Array of the nodes with this page mapped in */
    int allocated;     /* The amount of memory that has been allocated */
    int n_allocs;      /* The number of memory allocations */
    alloc_t **allocs;  /* List of allocated chunks of memory in the page */
} page_t;

typedef struct command_request {
    int    nid;      /* the node invoking this request */
    char  *command;  /* the request being made */
    struct command_request *next; /* The next message in the queue */
    struct command_request *prev; /* The previous message in the queue */
} request_t;

/* */
typedef struct allocator_information {
    int        n_nodes;     /* The number of nodes currently running */
    int        total_nodes; /* The total number of nodes to run */
    int        socket;      /* The socket used to receive connections */
    int       *c_sockets;   /* All of the connected client sockets */
    int        n_blocked    /* The number of */
    page_t   **page_list;   /* The pages in the shared memory map */
    request_t *m_queue;     /* A linked list of all of the node requests */
    request_t *m_last;      /* The last message in the queue */
} allocator_t;

int allocator_init(metadata_t *metadata, allocator_t *allocator);
int allocator_end (allocator_t *allocator);
int allocate      (metadata_t *metadata, allocator_t *allocator);
int fatal(char *message);

int message_add  (allocator_t *allocator, int nid, char buffer[]);
int message_rm   (allocator_t *allocator, request_t *request);

int node_init    (allocator_t *allocator, int client);
int node_execute (allocator_t *allocator);
int node_close   (allocator_t *allocator, request_t *request);
int node_barrier (allocator_t *allocator, request_t *request);
int node_allocate(allocator_t *allocator, request_t *request);
int node_cast    (allocator_t *allocator, request_t *request);
int handle_fault (allocator_t *allocator, request_t *request);

#endif