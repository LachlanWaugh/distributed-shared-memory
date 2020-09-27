#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
// #include "sm.h"

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
    char *hostfile_name;
    char *logfile_name;
    int n_node_processes;
    char *executable_filename;
    char **node_options;
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch(opt) {
            case 'H':
                hostfile_name = optarg;
                printf("hostfile: %s\n", hostfile_name);
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
    // printf("optind: %d\n", optind);
    // printf("argc: %d\n", argc);

    // Get executable filename
    if (optind < argc) {
        executable_filename = argv[optind];
        printf("Executable filename: %s\n", executable_filename);
        optind++;
    } else {
        printf("error: Missing argument EXECUTABLE-FILE\n");
        printf("%s\n", usage);
        exit(1);
    }

    // Collect node options
    if (optind < argc) {
        int length = argc - optind;
        node_options = malloc(sizeof(char) * 128 * length);
        printf("Node arguments:\n");
        for (int i = 0; i < length; i++) {
            node_options[i] = argv[optind];
            printf("%s\n", node_options[i]);
            optind++;
        }
    }
    return 0;
}