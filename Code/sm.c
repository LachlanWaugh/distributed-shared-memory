#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "dsm.h"
#include "sm.h"

int sm_node_init (int *argc, char **argv[], int *nodes, int *nid) {
    node_t *node;
    char *ip, *port;
    int sock, status;

    ip   = strndup(argv[0][1], NAME_LEN_MAX);
    port = strndup(argv[0][2], NAME_LEN_MAX);
    
    /* Create the node */
    node = malloc(sizeof(node_t *));
    node->nid = *nid;
    *(nid)++;

    /* Create the socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) {
        fprintf(stderr, "Failed to create socket.\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = ip;
    address.sin_port        = port;

    status = connect(sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) {
        fprintf(stderr, "Failed to connect socket.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

void sm_node_exit (void) {
    return;
}

void *sm_malloc (size_t size) {
    // TODO
    return NULL;
}

void sm_barrier (void) {
    return;
}

void sm_bcast (void **addr, int root_nid) {\
    // TODO
    return;
}