#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "dsm.h"
#include "config.h"
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

    /* Go into a loop to serve requests from the nodes until all of them have exited */
    result = allocate();
    if (result) {
        exit(EXIT_FAILURE);
    }

    /* De-allocate all of the memory used */
    result = clean();
    return result;
}

/* 
 * Free allocated memory
 */
int clean() {
    /* Free all of the memory associated with the allocator */
    allocator_end();

    free(options->program);

    if (options->host_names) {
        for (int i = 0; options->host_names[i] != NULL; i++) free(options->host_names[i]);
        free(options->host_names);
    }
    if (options->prog_args) {
        for (int i = 0; options->prog_args[i] != NULL; i++)  free(options->prog_args[i]);
        free(options->prog_args);
    }
    if (options->log_file) {
        fclose(options->log_file);
    }

    /* Finally, wait for all of the */
    // for (int i = 0; i < options->n_nodes; i++) {
    //     waitpid(global_pids[i], NULL, 0);
    // }
    
    free(options);

    return 0;
}