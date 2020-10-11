#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "dsm.h"
#include "sm.h"

int main(int argc, char **argv) {
    metadata_t metadata;
    int result = 0;

    /* Read and process the command-line arguments */
    result = setup(argc, argv, &metadata);
    if (result) exit(EXIT_FAILURE);

    /* Connect the node processes and start-up the allocator*/
    result = run(&metadata);
    if (result) exit(EXIT_FAILURE);

    /* Deallocate all of the memory used */
    clean(&metadata);

    return result;
}

/* Read and parse the command-line arguments */
int setup(int argc, char **argv, metadata_t *meta) {
    extern char *optarg;
    extern int optind, opterr, optopt;

    int opt = 0, n_proc = 1, n_node_opts = 0, n_hosts = 1;
    char *log_file = NULL, *host_file = NULL, *exe_file = NULL, 
         **host_names = NULL, **node_opts = NULL;

    /* Read and process the options */
    while ((opt = getopt(argc, argv, "H:hl:n:v")) != -1) {
        switch (opt) {
            case 'H':
                host_file = strndup(optarg, NAME_LEN_MAX);
                break;
            case 'h':
                fprintf(stderr, USAGE);
                exit(EXIT_SUCCESS);
                break;
            case 'l':
                log_file = strndup(optarg, NAME_LEN_MAX);
                break;
            case 'n':
                n_proc = strtol(optarg, NULL, 10);
                break;
            case 'v':
                fprintf(stdout, "version 1.0\n");
                break;
            default:
                fprintf(stderr, "Error: invalid option given '%c'\n", opt);
                return -1;
        }

    }

    /* Read the EXECUTABLE-FILEs name */
    if (optind < argc) {
        exe_file = strndup(argv[optind++], NAME_LEN_MAX);
    /* No executable file was provided */
    } else {
        fprintf(stderr, "Error: no EXECUTABLE-FILE provided, please use './dsm -h' to identify correct usage.\n");
        return -1;
    }

    /* Read and process the NODE-OPTIONs */
    if (optind < argc) {
        n_node_opts = argc - optind;
        node_opts = malloc(sizeof(char *) * n_node_opts);

        /* Read each node option into the array */
        for (int i = 0; i < OPT_MAX && i < n_node_opts; i++)
            node_opts[i] = strndup(argv[optind++], ARG_LEN_MAX);
        node_opts[n_node_opts] = NULL;
    }

    /* Read the host names file */
    read_hostfile(host_file, &host_names, &n_hosts);

    /* Store the information gathered into the metadata struct */ 
    meta->n_proc = n_proc, meta->n_node_opts = n_node_opts, meta->n_hosts = n_hosts;
    meta->exe_file = exe_file, meta->node_opts = node_opts; 
    meta->log_file = log_file, meta->host_names = host_names;

    return 0;
}

int run(metadata_t *metadata) {
    int *n_proc = malloc(sizeof(int *)), status;
    pid_t pid;

    allocator_t *allocator = malloc(sizeof(allocator_t));
    status = server_init(metadata, allocator);
    if (status) {
        fprintf(stderr, "Error: failed to initialize the server.\n");
        return -1;
    }

    /* Loop until you've created the required number of processes */
    while (*n_proc < metadata->n_proc) {
        pid = fork();
        (*n_proc)++;

        /* Parent process created */
        if (pid > 0) {
            fprintf(stderr, "\n--- %d ---\n", *n_proc);
            allocate(metadata, allocator);
            /* Wait for children to finish, allocate memory */
        /* Child process created */
        } else if (pid == 0) {
            /* Execute the program via ssh */
            status = execute(metadata, n_proc);
            if (status) exit(EXIT_FAILURE);
            else        exit(EXIT_SUCCESS);
        /* Error occurred  if fork() */
        } else {
            fprintf(stderr, "Error: fork() failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}

int execute(metadata_t *meta, int *n_proc) {
    int host_index = 0, status = 0;
    char command[COMMAND_LEN_MAX], *host_name, buffer[NAME_LEN_MAX];

    /* Loop around when insufficient numbers of hosts */
    host_index = (*n_proc < meta->n_hosts) ? *n_proc - 1 : (*n_proc - 1) % meta->n_hosts;
    host_name = meta->host_names[host_index];

    /* write the command to be executed on the target device */
    status = snprintf(command, COMMAND_LEN_MAX, "ssh %s %s", host_name, meta->exe_file);
    if (status == 0) {
        fprintf(stderr, "Error: failed to create command for '%s'\n", host_name);
        return -1;
    }

    /* Append the arguments to the command */
    for (int i = 0; i < meta->n_node_opts; i++) {
        status = snprintf(command + strlen(command), COMMAND_LEN_MAX, " %s", meta->node_opts[i]);
        if (status == 0) {
            fprintf(stderr, "Error: failed to create command for '%s'\n", host_name);
            return -1;
        }
    }

    /* Append the communication information to the command */
    gethostname(buffer, 1023);
    status = snprintf(command + strlen(command), COMMAND_LEN_MAX, " %s %d",
                        buffer, PORT);
    if (status == 0) {
        fprintf(stderr, "Error: failed to create command for '%s'\n", host_name);
        return -1;
    }

    /* ssh into the host and execute the program */
    status = system(command);
    if (status) {
        fprintf(stderr, "Error: failed to execute ssh into '%s'\n", host_name);
        return -1;
    }

    return 0;
}

int allocate(metadata_t *metadata, allocator_t *allocator) {
    struct sockaddr_in address;
    int client, addrlen = sizeof(address);
    char buffer[1024] = "\0";

    while(wait(NULL) > 0) {
        client = accept(allocator->socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client < 0) {
            fprintf(stderr, "Failed to accept connections.\n");
            return -1;
        }

        read(client, buffer, 1024);
        if (strcmp(buffer, "init") == 0) {
            /* Create the new node */
            node_t *node = malloc(sizeof(node_t));
            node->nid = allocator->n_nodes;

            /* Add it to the database */
            allocator->node_list[allocator->n_nodes] = node;
            allocator->n_nodes++;

            /* Return a message to the node, indicating n_nodes and it's nid */
            snprintf(buffer, MSG_LEN_MAX, "nodes: %d, nid: %d\n", 
                     node->nid, allocator->n_nodes);
            send(client, buffer, strlen(buffer), 0);
        }

        close(client);
    }

    return 0;
}

/* Free allocated memory */
int clean(metadata_t *meta) {
    for (int i = 0; i < meta->n_hosts; i++)
        free(meta->host_names[i]);
    if (meta->host_names) free(meta->host_names);

    for (int i = 0; i < meta->n_node_opts; i++)
        free(meta->node_opts[i]);
    if (meta->node_opts)  free(meta->node_opts);

    if (meta->log_file)   free(meta->log_file);
    if (meta->exe_file)   free(meta->exe_file);

    return 0;
}

int read_hostfile(char *host_file, char ***host_names, int *n_hosts) {
    /* Allocate memory for the host names */
    *host_names = malloc(sizeof(char *) * HOSTS_MAX);
    for (int i = 0; i < HOSTS_MAX; i++)
        (*host_names)[i] = NULL;

    /* If there is no hostfile specified, default to 'hosts' */
    if (host_file == NULL)
        host_file = strndup("hosts", 6);

    FILE *hostfile = fopen(host_file, "r");

    /* If the hostfile doesn't exist, use localhost instead */
    if (hostfile == NULL) {
        (*host_names)[0] = strndup("localhost", 10);
    /* Otherwise read from the hostfile */
    } else {
        char name_buff[NAME_LEN_MAX];
        int i = 0;
        /* Read the file for host file names */
        for (i = 0; fgets(name_buff, NAME_LEN_MAX, hostfile); i++) {
            name_buff[strlen(name_buff) - 1] = '\0'; /* Remove trailing newline */
            (*host_names)[i] = strndup(name_buff, NAME_LEN_MAX);
        }
        *n_hosts = i;

        /* If the file is empty, use localhost */
        if (i == 0) {
            (*host_names)[0] = strndup("localhost", 10);
        } else {
            /* NULL-terminate the array for later traversal */
            (*host_names)[i] = NULL;
        }

        fclose(hostfile);
    }

    return 0;
}

int server_init(metadata_t *metadata, allocator_t *allocator) {
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
    if (sock == 0) {
        fprintf(stderr, "Error: Failed to create socket.\n");
        return -1;
    }
    allocator->socket = sock;

    status = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if (status) {
        fprintf(stderr, "Error: failed setting socket options.\n");
        return -1;
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    status = bind(sock, (struct sockaddr *)&address, sizeof(address));
    if (status < 0) {
        fprintf(stderr, "Error: Failed to bind socket.\n");
        return -1;
    }

    status = listen(sock, metadata->n_proc);
    if (status < 0) {
        fprintf(stderr, "Error: Failed to listen to socket.\n");
        return -1;
    }

    return 0;
}