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
    int client_socket[allocator->total_nodes], addrlen = sizeof(address), status,
        sock, max_sock, activity, client;
    char buffer[1024] = "\0";

    /* Initialize all the client sockets to 0 */
    for (int i = 0; i < allocator->total_nodes; i++) client_socket[i] = 0;

    /* First, initialize all of the client nodes' sockets */
    while (allocator->n_nodes < allocator->total_nodes) {
        /* Check the socket for the new connection */
        client = accept(allocator->socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client < 0) return fatal("Failed to accept connections");

        /* Ensure that it is an initialization request */
        recv(client, buffer, 1023, 0);
        if (strcmp(buffer, "init")) 
            return fatal("invalid initialization request");

        /* Add it's socket to the list */
        for (int i = 0; i < allocator->total_nodes; i++) {
            if (client_socket[i] == 0) {
                client_socket[i] = client;
                break;
            }
        }

        /* Initailize the new node and socket */
        status = node_init(allocator, client);
        if (status) return fatal("Node initialization failed");
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
            sock = client_socket[i];
            if (sock > 0)        FD_SET(sock, &fds);
            if (sock > max_sock) max_sock = sock;
        }

        activity = select(max_sock+1, &fds, NULL, NULL, NULL);
        /* Read messages coming in from the existing nodes */
        for (int i = 0; i < allocator->total_nodes; i++) {
            if (FD_ISSET(client_socket[i], &fds)) {
                recv(client_socket[i], buffer, 1023, 0);

                /* Parse the received request */
                status = node_execute(allocator, i, client_socket, buffer);
                if (status) return fatal("Failed to execute command");
            }
        }
    }

    close(allocator->socket);
    return 0;
}

/* 
 * The execution of command was extracted into a separate function for reuse
 * the barrier function
*/
int node_execute(allocator_t *allocator, int client_index, int client_list[], char buffer[]) {
    if (strstr(buffer, "close")) {
        node_close(allocator, client_list[client_index], buffer);
        client_list[client_index] = 0;
        close(client_list[client_index]);
    } else if (strcmp(buffer, "barrier") == 0) {
        node_barrier(allocator, client_index, client_list);
    } else {
        return fatal("Invalid mesage received");
    }

    return 0;
}

int node_init(allocator_t *allocator, int client) {
    char buffer[1024];

    /* Create the new node */
    node_t *node = malloc(sizeof(node_t));
    node->nid = allocator->n_nodes;
    node->socket = client;

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
    sscanf(buffer, "close node %d", &client_id);

    /* NULL out the node */
    free(allocator->node_list[client_id]);
    allocator->node_list[client_id] = NULL;
    allocator->n_nodes--;

    /* Return a message to the node, indicating it has been closed */
    snprintf(buffer, MSG_LEN_MAX, "close ACK");
    send(client, buffer, strlen(buffer), 0);

    return 0;
}

int node_barrier(allocator_t *allocator, int client_index, int client_list[]) {
    char buffer[1024];

    /*
     * Wait for all clients to have sent a barrier request, completing their
     * pre-existing tasks to synchronise the nodes
    */
    for (int i = 0; i < allocator->total_nodes; i++) {
        /* Don't check the client that initiated the barrier */
        if (i == client_index)   continue;
        if (client_list[i] == 0) continue;

        while(1) {
            recv(client_list[i], buffer, 1023, 0);
            if (strcmp(buffer, "barrier") == 0) break;
            
            /* If the request isn't a barrier, execute it */
            node_execute(allocator, i, client_list, buffer);
        }
    }

    /* Once all of the nodes have completed the barrier, send them a ACK */
    for (int i = 0; i < allocator->total_nodes; i++) {
        if (client_list[i] == 0) continue;
        send(client_list[i], "barrier ACK", 12, 0);
    }

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
    allocator->total_nodes = metadata->n_proc;

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