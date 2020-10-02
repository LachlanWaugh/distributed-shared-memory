#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#include "dsm.h"
#include "sm.h"

int main(int argc, char **argv) {
    metadata_t metadata;
    int result = 0;

    result = setup(argc, argv, &metadata);
    if (result) exit(EXIT_FAILURE);

    result = run(&metadata);
    if (result) exit(EXIT_FAILURE);

    //clean(&metadata);

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
                exit(EXIT_FAILURE);
        }

    }

    /* Read the EXECUTABLE-FILEs name */
    if (optind < argc) {
        exe_file = strndup(argv[optind++], NAME_LEN_MAX);
    /* No executable file was provided */
    } else {
        clean(meta);
        fprintf(stderr, "Error: no EXECUTABLE-FILE provided, please use './dsm -h' to identify correct usage.\n");
        exit(EXIT_FAILURE);
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

int run(metadata_t *meta) {
    int *n_proc = malloc(sizeof(int *)), status = 0;
    pid_t pid;

    /* Loop until you've created the required number of processes */
    while (*n_proc < meta->n_proc) {
        pid = fork();
        (*n_proc)++;

        /* Parent process created */
        if (pid > 0) {
            /* Wait for children to finish */
            while(wait(NULL) > 0);
        /* Child process created */
        } else if (pid == 0) {
            status = execute(meta, n_proc);
            if (status == 0) exit(EXIT_FAILURE);
            exit(EXIT_SUCCESS);
        /* Error occurred */
        } else {
            fprintf(stderr, "Error: fork() failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}

int execute(metadata_t *meta, int *n_proc) {
    int host_index = 0, status = 0;
    char command[COMMAND_LEN_MAX];

    /* Loop around when insufficient numbers of hosts */
    host_index = (*n_proc < meta->n_hosts)
                ? *n_proc - 1 : (*n_proc - 1) % meta->n_hosts;

    /* ssh into the host and execute EXECUTABLE-FILE */
    status = snprintf(command, COMMAND_LEN_MAX, "ssh %s %s",
                        meta->host_names[host_index], meta->exe_file);
    if (status == 0) exit(EXIT_FAILURE);

    /* Append the arguments to the command */
    for (int i = 0; i < meta->n_node_opts; i++)
        status = snprintf(command + strlen(command), COMMAND_LEN_MAX, " %s", meta->node_opts[i]);
    if (status == 0) exit(EXIT_FAILURE);

    status = system(command);
    if (status == 0) exit(EXIT_FAILURE);

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