#include <stdlib.h>
#include "dsm.h"

#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#define PORT 9243

/* */
typedef struct node {
    int   nid;                /* The identifier for the node */
    int   socket;             /* The socket used to communicate with the node */
    char *host_name;          /* The host this node is running on */
} node_t;

/* */
typedef struct memory_page {
    int permissions;    /* The current permissions (read/write/none) */
    int owner;          /* The node that has write permissions for this page */
    int mapped[];       /* An array of the nodes that have this physically mapped */
} page_t;

/* */
typedef struct allocator_information {
    int      n_nodes;           /* The number of nodes currently running */
    int      total_nodes;       /* The total number of nodes to run */
    int      socket;            /* The socket used to receive connections */
    node_t **node_list;         /* The client nodes */
    page_t **page_list;         /* The pages in the shared memory map */
} allocator_t;

int allocator_init(metadata_t *metadata, allocator_t *allocator);
int allocate(metadata_t *metadata, allocator_t *allocator);
int fatal(char *message);

int node_execute (allocator_t *allocator, int client_index, int client_list[], char buffer[]);
int node_init    (allocator_t *allocator, int client);
int node_close   (allocator_t *allocator, int client, char buffer[]);
int node_barrier (allocator_t *allocator, int client_index, int client_list[]);
int node_allocate(allocator_t *allocator, int client, char buffer[]);
int node_cast(allocator_t *allocator, int client, int client_list[], char buffer[]);

#endif