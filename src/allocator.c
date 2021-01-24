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
#include <sys/mman.h>

#include "allocator.h"
#include "config.h"
#include "node_functions.h"

int sm_fatal(char *message) {
    fprintf(stderr, ANSI_COLOR_RED "Error: %s.\n" ANSI_COLOR_RESET, message);
    return -1;
}

/* 
 *
*/
int allocator_init() {
    int status = 0;

    /* Prepare the shared memory mapping and information */
    /* Keep a cache of the memory map in the allocator to reduce the overhead of read-faults */
    sm_memory_map = mmap((void *)SM_MAP_START, SM_NUM_PAGES * getpagesize(), 
                PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (sm_memory_map == MAP_FAILED) return sm_fatal("failed to map memory");
    sm_current_page = 0;
    
    sm_node_count = 0;

    /* Create and initialize the list of memory pages */
    for (int i = 0; i < SM_MAX_PAGES; i++) {
        sm_page_table[i].writer = -1;
        sm_page_table[i].readers = malloc(options->n_nodes * sizeof(int));

        for (int j = 0; j < options->n_nodes; j++) {
            sm_page_table[i].readers[j] = 0;
        }
    }

    /* Initialize all the client sockets to 0 */
    client_sockets = malloc(options->n_nodes * sizeof(int));
    for (int i = 0; i < options->n_nodes; i++) {
        client_sockets[i] = 0;
    }

    /*  */
    if (options->log_file) {
        fprintf(options->log_file, "-= %d node processes\n", options->n_nodes);
    } else {
        options->log_file = NULL;
    }

    /* Initialize the global listening socket to listen for new connections */
    status  = socket_init();
    if (status) return sm_fatal("failed to initialize global listen socket.");

    return 0;
}

/*
 *
*/
int socket_init() {
    struct sockaddr_in address;
    int opt = 1, client, addrlen = sizeof(address);

    /* Create the communication socket */
    sm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (sm_socket == 0) return sm_fatal("failed to create socket");

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(SM_PORT);

    status = setsockopt(sm_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if (status < 0) return sm_fatal("failed setting socket options");
    status = bind(sm_socket, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) return sm_fatal("failed to bind socket");
    status = listen(sm_socket, options->n_nodes);
    if (status < 0) return sm_fatal("failed to listen to socket");

    return 0;
}

/*
 *
*/
int allocator_end() {
    /* Free the page list  */
    for (int i = 0; i < SM_MAX_PAGES; i++) {
        free(sm_page_table[i].writers);
    }

    /* Free the list of client sockets */
    free(client_sockets);

    if (options->log_file)
        fclose(options->log_file);
    
    close(sm_socket);

    return 0;
}

/*
 *
*/
int allocate() {
    int status, activity;
    msg_t *request

    /*
     * Wait for messages from the clients to come in, running until all nodes have been closed
    */
    while(sm_node_count > 0) {
        fd_set fds = wait_for_messages();
        
        /* Check each client to see if they have any pending requests */
        for (int i = 0; i < options->n_nodes; i++) {
            if (FD_ISSET(client_sockets[i], &fds)) {
                memset(request, 0, sizeof(msg_t) + 1024);
                sm_recv(client_sockets[i], &request);

                /* Execute the received request */
                status = node_execute(i, request);
                if (status) return sm_fatal("failed to execute command");

                sm_msg_free(request);
            }
        }
    }

    while(wait(NULL) > 0);
    return 0;
}

/*
 *
*/
int wait_for_messages() {
    fd_set fds;
    int max_sock, activity;

    FD_ZERO(&fds);
    max_sock = 0;

    /* Initialize the list of client sockets */
    for (int i = 0; i < options->n_nodes; i++) {
        if (client_sockets[i] > 0)
            FD_SET(client_sockets[i], &fds);

        if (client_sockets[i] > max_sock)
            max_sock = client_sockets[i];
    }

    activity = select(max_sock+1, &fds, NULL, NULL, NULL);

    return fds;
}