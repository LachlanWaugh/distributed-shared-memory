#include <stdlib.h>
#include "dsm.h"

#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#define PORT 9243

/* */
typedef struct node {
    int   nid;                /* The identifier for the node */
    char *host_name;          /* The host this node is running on */
} node_t;

/* */
typedef struct allocator_information {
    int      n_nodes;           /* The number of nodes */
    node_t **node_list;         /* A list of all the nodes */
    int     socket;             /* The socket used to receive connections */
} allocator_t;

int allocator_init(metadata_t *metadata, allocator_t *allocator);
int node_init(allocator_t *allocator, int client, char buffer[]);
int node_close(allocator_t *allocator, int client, char buffer[]);
int allocate(metadata_t *metadata, allocator_t *allocator);

int fatal(char *message);

#endif