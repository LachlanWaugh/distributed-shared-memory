#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
// #include "sm.h"

#define MAX_HOSTNAME_LENGTH 255
#define MAX_ARGUMENT_LENGTH 256
#define MAX_COMMAND_LENGTH 2048
#define MAX_HOSTS 64

int read_hostnames_from_file(char * filename, char **hostnames);

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
    int n_hostnames;
    char **hostnames = malloc(sizeof(char*) * MAX_HOSTS);
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
                n_hostnames = read_hostnames_from_file(hostfile_name, hostnames);
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
        n_hostnames = read_hostnames_from_file("hosts", hostnames);
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
            int status;
            int exited_id = wait(&status);
            if (exited_id == -1) {
                printf("error: wait\n");
            }
            if (WIFEXITED(status)) {
                printf("Process [%d] exited with status %d\n", exited_id, WEXITSTATUS(status));
            } 
            else if (WIFSIGNALED(status)) {
                printf("Process [%d] killed by signal %d\n", exited_id, WTERMSIG(status));
            } 
            else if (WIFSTOPPED(status)) {
                printf("Process [%d] stopped by signal %d\n", exited_id, WSTOPSIG(status));
            } 
            else if (WIFCONTINUED(status)) {
                printf("Process [%d] continued\n", exited_id);
            }
        } 
        // Child process
        else if (proc_id == 0) {
            printf("This is child process [%d]\n", getpid());
            int allocated_hostname_index;
            if (helper_proc_count < n_hostnames) {
                allocated_hostname_index = helper_proc_count - 1;
            }
            else {
                allocated_hostname_index = (helper_proc_count - 1) % n_hostnames;
            }
            printf("attempting to run node process on: %s\n", hostnames[allocated_hostname_index]);
            char node_process_command[MAX_COMMAND_LENGTH];
            int sprintf_status = sprintf(node_process_command, "ssh %s %s", hostnames[allocated_hostname_index], executable_filename);
            printf("sprintf_status:%d\n", sprintf_status);
            int err = system(node_process_command);
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
    if (hostnames == NULL) {
        printf("hostnames is null\n");
    }
    for (int i = 0; i < MAX_HOSTS; i++) {
        if (hostnames[i] != NULL) {
            free(hostnames[i]);
        }
        else {
            break;
        }
    }
    free(hostnames);

    return 0;
}

/* Reads hostnames from a file into an array of strings and returns the number of hostnames */
int read_hostnames_from_file(char * filename, char **hostnames) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        // If file cannot be opened, use localhost
        hostnames[0] = malloc(sizeof(char) * MAX_HOSTNAME_LENGTH);
        strcpy(hostnames[0], "localhost");
        return 1;
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
        fclose(fp);
        // If file is empty use localhost
        if (i == 0) {
            hostnames[0] = malloc(sizeof(char) * MAX_HOSTNAME_LENGTH);
            strcpy(hostnames[0], "localhost");
            return 1;
        }
        return i;
    }
}