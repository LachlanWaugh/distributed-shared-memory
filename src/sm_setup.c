#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>

#include "sm_setup.h"
#include "allocator.h"
#include "config.h"

/* */
int setup(int argc, char **argv) {
    int result = 0;

    /* Process the command-line options and write them into the global options variable */
    result = process_arguments(argc, argv);
    if (result) return result;

    /* Start the allocator to receive messages from the clients */
    result = allocator_init();
    if (result) return result;

    /* Start up all the nodes and initailize communication with them */
    result = initialize();
    if (result) return result;

    return result;
}

/* Initialize the data structure to store the command-line options (number of nodes, program to run ect) */
int options_init() {
    options = malloc(sizeof(options));
    options->n_nodes  = 1;
    options->log_file = NULL;
    
    /* Allocate memory for the host names */
    options->host_names = malloc(sizeof(char *) * SM_HOSTS_MAX);
    for (int i = 0; i < SM_HOSTS_MAX; i++) {
        options->host_names[i] = NULL;
    }

    options->prog_args = NULL;

    return 0;
}

/*
 * Read and parse the command-line arguments
 */
int process_arguments(int argc, char **argv) {
    extern char *optarg;
    extern int optind;
    int opt;

    /* Initialize the data structure to store the arguments given to dsm */
    int result = options_init();
    if (result) return result;

    /* Read and process the options */
    while ((opt = getopt(argc, argv, "H:hl:n:v")) != -1) {
        switch (opt) {
            case 'H':
                options->host_names[0] = strndup(optarg, SM_LEN_MAX);
                break;
            case 'h':
                fprintf(stderr, "%s", USAGE);
                exit(EXIT_SUCCESS);
                break;
            case 'l':
                // Attempt to open the log_file and store it's fd
                options->log_file = fopen(optarg, "w+");
                // TODO: Check if opening the log file failed
                break;
            case 'n':
                options->n_nodes = strtol(optarg, NULL, 10);
                break;
            case 'v':
                fprintf(stdout, "version 1.0\n");
                break;
            default:
                fprintf(stderr, "Error: invalid option given '%c'\n", opt);
                return -1;
        }
    }

    /* */
    result = process_program(argc, argv, optind);
    if (result) return result;

    /* Read the host names file */
    result = read_hostfile();
    if (result) return result;

    return 0;
}

/*
 * Parse the options regarding the program to be executed across the nodes (program-name and it's arguments)
 */
int process_program(int argc, char **argv, int optind) {
    char *program = NULL, **prog_args = NULL;

    /* Read the EXECUTABLE-FILEs name */
    if (optind < argc) {
        program = strndup(argv[optind++], SM_LEN_MAX);
    /* No executable file was provided */
    } else {
        return sm_fatal("no EXECUTABLE-FILE provided, please use './dsm -h' to identify correct usage");
    }

    /* Read and process the NODE-OPTIONs */
    if (optind < argc) {
        int n_prog_args = argc - optind;
        prog_args = malloc(sizeof(char *) * n_prog_args);

        /* Read each node option into the array */
        for (int i = 0; i < SM_ARG_MAX && i < n_prog_args; i++)
            prog_args[i] = strndup(argv[optind++], SM_LEN_MAX);
        
        /* NULL-terminate the array to prevent reading unallocated memory */
        prog_args[n_prog_args] = NULL;
    }

    /* Store the information gathered into the options struct */ 
    options->program = program, options->prog_args = prog_args;

    return 0;
}

/*
 * Extract the host-names from the provided host-name file
 */
int read_hostfile() {
    char **host_names = options->host_names, *host_filename;

    /* If there is no hostfile specified, default to 'hosts' */
    if (host_names[0] == NULL) {
        host_filename = strndup("hosts", 6);
    /* Otherwise use the specified host file */
    } else {
        host_filename = strndup(host_names[0], SM_LEN_MAX);
    }

    FILE *host_file = fopen(host_filename, "r");

    /* If the hostfile (or 'hosts' if none supplied) doesn't exist, use localhost instead */
    if (host_file == NULL) {
        host_names[0] = strndup("localhost", 10);
    /* Otherwise read from the host_file */
    } else {
        char name_buff[SM_LEN_MAX];
        int i = 0;

        /* Read the file for host file names */
        for (i = 0; fgets(name_buff, SM_LEN_MAX, host_file); i++) {
            name_buff[strlen(name_buff) - 1] = '\0'; /* Remove trailing newline */
            host_names[i] = strndup(name_buff, SM_LEN_MAX);
        }

        options->n_hosts = i;

        /* If the file is empty, use localhost */
        if (i == 0) {
            host_names[0] = strndup("localhost", 10);
        } else {
            /* NULL-terminate the array for later traversal */
            host_names[i] = NULL;
        }

        fclose(host_file);
    }

    free(host_filename);

    return 0;
}

/*
 * First start up all of the nodes via ssh, then setup communication sockets with all of the nodes.
 */
int initialize() {
    int status;

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
            sm_fatal("fork() failed");
            exit(EXIT_FAILURE);
        }
    }

    /* Reset the node_count for use in initializing communication with the nodes */
    sm_node_count = 0;
    
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    /* Now, initialize all of the client nodes' sockets */
    while (sm_node_count < options->n_nodes) {
        /* Check the socket for the new connection */
        int client = accept(sm_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client < 0) {
            return sm_fatal("Failed to accept connections");
        }

        /* Initailize the new node and socket */
        status = node_init(client);
        if (status < 0) {
            return sm_fatal("Node initialization failed");
        }
    }

    /* */
    return 0;
}

/*
 *
 */
int node_start() {
    int host_index = 0, status = 0;
    char command[SM_LEN_MAX], buffer[SM_LEN_MAX];

    /* Loop around when insufficient numbers of hosts */
    host_index = (sm_node_count - 1) % options->n_hosts;

    /* write the command to be executed on the target device */
    status = snprintf(command, SM_LEN_MAX, "ssh %s %s", options->host_names[host_index], options->program);
    if (status == 0) {
        return sm_fatal("failed to create command");
    }

    /* Append the arguments to the command */
    for (int i = 0; options->prog_args[i] != NULL; i++) {
        status = snprintf(command + strlen(command), SM_LEN_MAX, " %s", options->prog_args[i]);
        if (status == 0) {
            return sm_fatal("failed to add arguments to command");
        }
    }

    /* Append the communication information (ip/port) to the command */
    gethostname(buffer, 1023);
    status = snprintf(command + strlen(command), SM_LEN_MAX, " %s %d", buffer, SM_PORT);
    if (status == 0) return sm_fatal("failed to add ip/port to command");

    /* ssh into the host and execute the program */
    status = system(command);
    if (status < 0) return sm_fatal("failed to execute ssh");

    return 0;
}