#include "sm_message.h"

#ifndef NODE_FUNCTIONS_H
#define NODE_FUNCTIONS_H

int node_init    (int socket);
int node_close   (int nid);

int node_execute (msg_t *request);

int node_wait    (int nid, int mode, void **ret, int root);
int node_barrier (int nid);
int node_allocate(int nid, char request[]);
int node_cast    (int nid, char request[]);
int handle_fault (int nid, char request[]);

#endif