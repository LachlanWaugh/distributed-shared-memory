#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
// #include "sm.h"

#define MAX_HOSTNAME_LENGTH 255
#define MAX_ARGUMENT_LENGTH 256
#define MAX_HOSTS 10

void read_hostnames_from_file(char * filename, char **hostnames);

int main (int argc, char **argv) {
    
    static char usage[] = "Usage: dsm [OPTION]... EXECUTABLE-FILE NODE-OPTION...\n\n  "
                           "-H HOSTFILE list of host names\n  "
                           "-h          this usage message\n  "
                           "-l LOGFILE  log each significant allocator action to LOGFILE\n              "
                           "(e.g., read/write fault, invalidate request)\n  "
                           "-n N        start N node processes\n  "
                           "-v          print version information\n\n"
                           "Starts the allocator, which starts N copies (one copy if -n not given) of\n"
                           "EXECUTABLE-FILE.  The NODE-OPTIONs are passed as arguments to the node\n"
                           "processes.  The hosts on which node processes are started are given in\n"
                           "HOSTFILE, which defaults to `hosts'.  If the file does not exist,\n"
                           "`localhost' is used.";
    
    extern char *optarg;
    extern int optind;
    int opt = 0;
    const char *optstring = "H:hl:n:v";
    char *hostfile_name = NULL;
    // Create hostnames array and initialise to NULL
    char **hostnames = malloc(sizeof(char*) * MAX_HOSTS);
    for (int i = 0; i < MAX_HOSTS; i++) {
        hostnames[i] = NULL;
    }
    char *logfile_name = NULL;
    int n_node_processes = 1; // default to 1
    char *executable_filename;
    char **node_options = NULL;
    int node_options_length = 0;
    // Use getopt to retrieve command line options
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch(opt) {
            case 'H':
                strcpy(hostfile_name, optarg);
                printf("hostfile: %s\n", hostfile_name);
                read_hostnames_from_file(hostfile_name, hostnames);
                break;
            case 'h':
                printf("%s\n", usage);
                break;
            case 'l':
                strcpy(logfile_name, optarg);
                printf("logfile: %s\n", logfile_name);
                break;
            case 'n':
                n_node_processes = atoi(optarg);
                printf("n_processes: %d\n", n_node_processes);
                break;
            case 'v':
                printf("dsm version 1.0\n");
                break;
            case '?':
                exit(EXIT_FAILURE);
                break;
        }
    }

    // Get executable filename
    if (optind < argc) {
        executable_filename = argv[optind];
        printf("Executable filename: %s\n", executable_filename);
        optind++;
    } else {
        printf("error: Missing mandatory argument EXECUTABLE-FILE\n");
        exit(EXIT_FAILURE);
    }

    // Collect node options
    if (optind < argc) {
        node_options_length = argc - optind;
        node_options = malloc(sizeof(char*) * node_options_length);
        for (int i = 0; i < node_options_length; i++) {
            node_options[i] = malloc(sizeof(char) * MAX_ARGUMENT_LENGTH);
        }
        printf("Node options:\n");
        for (int i = 0; i < node_options_length; i++) {
            strcpy(node_options[i], argv[optind]);
            printf("%s\n", node_options[i]);
            optind++;
        }
    }

    // If hostnames are not set (-H flag was not used)
    if (hostnames[0] == NULL) {
        // Read from 'hosts' instead
        read_hostnames_from_file("hosts", hostnames);    
    }

    // Fork helper processes
    int helper_proc_count = 0;
    pid_t proc_id;
    while (helper_proc_count < n_node_processes) {
        proc_id = fork();
        helper_proc_count++;
        // Parent process
        if (proc_id > 0) {
            printf("Parent [%d]: successfully forked %d\n", getpid(), proc_id);
            // Wait for child processes
            for (int i = 0; i < helper_proc_count; i++) {
                int exited_id = wait(NULL);
                printf("process exited with id: %d\n", exited_id);
            }
        } 
        // Child process
        else if (proc_id == 0) {
            printf("This is child process [%d]\n", getpid());
            int err = system("ssh vina01 ~/comp9243/distributed-shared-memory/hello");
            printf("err: %d\n", err);
            exit(EXIT_SUCCESS);
        }
        // Fork failed
        else {
            printf("error: failed to fork\n");
            exit(EXIT_FAILURE);
        }
    }
    


    // Free memory
    if (node_options != NULL) {
        for (int i = 0; i < node_options_length; i++) {
            free(node_options[i]);
        }
        free(node_options);
    }
    
    for (int i = 0; i < MAX_HOSTS; i++) {
        free(hostnames[i]);
    }
    free(hostnames);

    return 0;
}

/* Reads hostnames from a file into an array of strings */
void read_hostnames_from_file(char * filename, char **hostnames) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        // If file cannot be opened, use localhost
        hostnames[0] = malloc(sizeof(char) * MAX_HOSTNAME_LENGTH);
        strcpy(hostnames[0], "localhost");
    } 
    else {
        // Read hostnames from the file using fgets
        char hostname[MAX_HOSTNAME_LENGTH];
        int i = 0;
        while (fgets(hostname, MAX_HOSTNAME_LENGTH, fp) != NULL) {
            hostname[strcspn(hostname, "\n")] = 0; // strip newline
            printf("hostname: %s\n", hostname);
            hostnames[i] = malloc(sizeof(char) * MAX_HOSTNAME_LENGTH);
            strcpy(hostnames[i], hostname);
            i++;
        }
        // If file is empty use localhost
        if (i == 0) {
            hostnames[0] = malloc(sizeof(char) * MAX_HOSTNAME_LENGTH);
            strcpy(hostnames[0], "localhost");
        }
        fclose(fp);
    }
    /* DEBUG */
    // Print hostnames
    printf("Hostnames:\n");
    for (int i = 0; i < MAX_HOSTS; i++) {
        if (hostnames[i] != NULL) {
            printf("%s\n", hostnames[i]);
        }
    }
    /* END DEBUG */
}