#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "dsm.h"
#include "allocator.h"
#include "sm_setup.h"
#include "sm_message.h"

int main(int argc, char **argv) {
    int result = 0;

    /* Read and process the command-line arguments */
    result = setup(argc, argv);
    if (result) {
        exit(EXIT_FAILURE);
    }

    /* Connect the node processes and start-up the allocator*/
    result = run();
    if (result) {
        exit(EXIT_FAILURE);
    }

    /* De-allocate all of the memory used */
    result = clean();
    return result;
}

int fatal(char *message) {
    fprintf(stderr, "Error: %s.\n", message);
    return -1;
}

/* 
 * Free allocated memory
 */
int clean() {
    free(options->program);

    if (options->host_names) {
        for (int i = 0; options->host_names[i] != NULL; i++) {
            free(options->host_names[i]);
        }

        // free(options->host_names);
    }

    if (options->prog_args) {
        for (int i = 0; options->prog_args[i] != NULL; i++) {
            free(options->prog_args[i]);
        }

        free(options->prog_args);
    }

    if (options->log_file) {
        fclose(options->log_file);
    }

    return 0;
}

/*
 *
 */
int run() {    
    /* Start  the allocator to receive messages from the clients */
    int status = allocator_init();
    if (status) {
        fatal("failed to initialize the server");
    }

    /* Loop until you've created the required number of processes */
    while (sm_node_count < options->n_nodes) {
        pid_t pid = fork();
        sm_node_count++;

        /* Parent process created */
        if (pid > 0) {
            continue;
            /* Wait for children to finish, allocate memory */
        /* Child process created */
        } else if (pid == 0) {
            /* Execute the program via ssh */
            status = node_start();
            if (status) exit(EXIT_FAILURE);
            else        exit(EXIT_SUCCESS);
        /* Error occurred if fork() */
        } else {
            fatal("fork() failed");
            exit(EXIT_FAILURE);
        }
    }

    return 0; // return allocate();
}