#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
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
    
    //printf("%s\n", usage);
    extern char *optarg;
    extern int optind;
    int opt = 0;
    const char *optstring = "H:hl:n:v";
    char *hostfile_name = NULL;
    char **hostnames = NULL;
    int hostnames_set_flag = 0;
    char *logfile_name;
    int n_node_processes;
    char *executable_filename;
    char **node_options;

    // Use getopt to retrieve command line options
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch(opt) {
            case 'H':
                hostfile_name = optarg;
                printf("hostfile: %s\n", hostfile_name);
                read_hostnames_from_file(hostfile_name, hostnames);
                break;
            case 'h':
                printf("%s\n", usage);
                break;
            case 'l':
                logfile_name = optarg;
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
                printf("error: invalid argument");
                exit(1);
                break;
        }
        
    }

    // Get executable filename
    if (optind < argc) {
        executable_filename = argv[optind];
        printf("Executable filename: %s\n", executable_filename);
        optind++;
    } else {
        printf("error: Missing argument EXECUTABLE-FILE\n");
        printf("%s\n", usage);
        exit(EXIT_FAILURE);
    }

    // Collect node options
    if (optind < argc) {
        int length = argc - optind;
        node_options = malloc(sizeof(char*) * length);
        for (int i = 0; i < length; i++) {
            node_options[i] = malloc(sizeof(char) * MAX_ARGUMENT_LENGTH);
        }
        printf("Node arguments:\n");
        for (int i = 0; i < length; i++) {
            strcpy(node_options[i], argv[optind]);
            printf("%s\n", node_options[i]);
            optind++;
        }
    }

    // If hostnames are not set (-H flag was not used)
    if (hostnames == NULL) {
        // Read from 'hosts' instead
        read_hostnames_from_file("hosts", hostnames);
    }
    return 0;
}

/* Reads hostnames from a file into an array of strings*/
void read_hostnames_from_file(char * filename, char **hostnames) {
    hostnames = malloc(sizeof(char*) * MAX_HOSTS);
    for (int i = 0; i < MAX_HOSTS; i++) {
        hostnames[i] = malloc(sizeof(char) * MAX_HOSTNAME_LENGTH);
    }
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("error: unable to open file: %s\n", filename);
        exit(EXIT_FAILURE);
    } 
    // Read hostnames from the file using fgets
    char hostname[MAX_HOSTNAME_LENGTH];
    int i = 0;
    while (fgets(hostname, MAX_HOSTNAME_LENGTH, fp) != NULL) {
        hostname[strcspn(hostname, "\n")] = 0; // strip newline
        printf("hostname: %s\n", hostname);
        strcpy(hostnames[i], hostname);
        i++;
    }
    fclose(fp);

    /* DEBUG */
    // Check hostnames
    printf("Hostnames:\n");
    for (int i = 0; i < MAX_HOSTS; i++) {
        printf("%s\n", hostnames[i]);
    }
    /* END DEBUG */
}