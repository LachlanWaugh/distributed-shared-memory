#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/wait.h>
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
    int client, addrlen = sizeof(address);
    char buffer[1024] = "\0";

    /* Wait for messages from the clients to come in */
    while(1) {
        client = accept(allocator->socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client < 0) return fatal("Failed to accept connections");

        read(client, buffer, 1023);
        /* Parse the received request */
        if (strcmp(buffer, "init") == 0) {
            node_init(allocator, client, buffer);
        } else if (strstr(buffer, "close") == 0) {
            node_close(allocator, client, buffer);
        }

        close(client);

        if (allocator->n_nodes <= 0) {
            return 0;
        }
    }
}

int node_init(allocator_t *allocator, int client, char buffer[]) {
    /* Create the new node */
    node_t *node = malloc(sizeof(node_t));
    node->nid = allocator->n_nodes;

    /* Add it to the database */
    allocator->node_list[allocator->n_nodes] = node;
    allocator->n_nodes++;

    /* Return a message to the node, indicating n_nodes and it's nid */
    snprintf(buffer, MSG_LEN_MAX, "nid: %d, nodes: %d\n", 
                node->nid, allocator->n_nodes);
    send(client, buffer, strlen(buffer), 0);

    return 0;
}

int node_close(allocator_t *allocator, int client, char buffer[]) {
    int client_id;
    
    /* Find the client's nid from the message */
    sscanf(buffer, "close nid=%d", &client_id);

    /* NULL out the node */
    free(allocator->node_list[client_id]);
    allocator->node_list[client_id] = NULL;
    allocator->n_nodes--;

    /* Return a message to the node, indicating it has been closed */
    send(client, "ACK", strlen(buffer), 0);

    return 0;
}

int allocator_init(metadata_t *metadata, allocator_t *allocator) {
    struct sockaddr_in address;
    int status, sock, opt = 1;

    /* Create the list to store all of the node processes */
    node_t **node_list = malloc(metadata->n_proc * sizeof(node_t *));
    for (int i = 0; i <= metadata->n_proc; i++)
        node_list[i] = NULL;
    allocator->node_list = node_list;
    allocator->n_nodes = 0;

    /* Create the communication socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) return fatal("failed to create socket");
    allocator->socket = sock;

    status = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if (status < 0) return fatal("failed setting socket options");

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    status = bind(sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) return fatal("failed to bind socket");

    status = listen(sock, metadata->n_proc);
    if (status < 0) return fatal("failed to listen to socket");

    return 0;
}