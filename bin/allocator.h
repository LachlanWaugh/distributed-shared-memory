#include <stdlib.h>
#include "dsm.h"

#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

int allocator_init();
int allocate      ();
int allocator_end ();

int node_init    (int client);
int node_wait    (int nid, char *message, void **ret, int root);
int node_execute (int nid, char request[]);
int node_close   (int nid, char request[]);
int node_barrier (int nid, char request[]);
int node_allocate(int nid, char request[]);
int node_cast    (int nid, char request[]);
int handle_fault (int nid, char request[]);

#endif