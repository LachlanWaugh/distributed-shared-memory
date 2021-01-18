#include <stdio.h>
#include <stdlib.h>

#ifndef _SM_SETUP_H
#define _SM_SETUP_H

#define USAGE "Usage: dsm [OPTION]... EXECUTABLE-FILE NODE-OPTION...\n\n\
    -H HOSTFILE list of host names\n\
    -h          this usage message\n\
    -l LOGFILE  log each significant allocator action to LOGFILE\n\
                (e.g., read/write fault, invalidate request)\n\
    -n N        start N node processes\n\
    -v          print version information\n\n\
Starts the allocator, which starts N copies (one copy if -n not given) of \
EXECUTABLE-FILE.  The NODE-OPTIONs are passed as arguments to the node \
processes.  The hosts on which node processes are started are given in \
HOSTFILE, which defaults to `hosts'.  If the file does not exist, \
`localhost' is used.\n"

#define SM_PORT      9243
#define SM_MSG_MAX   128
#define SM_PAGESIZE  1024
#define SM_MAX_PAGES 1000

#define SM_LEN_MAX 128
#define SM_ARG_MAX 32
#define SM_HOSTS_MAX 10

/* */
struct options {
    int    n_nodes;    /* The number of nodes required */   
    FILE  *log_file;   /* The log file for the operations performed */
    char **host_names; /* A list of host-names (addresses of the nodes) */

    char *program;     /* The name of the program to be ran */
    char **prog_args;  /* The arguments to be passed to the program */
};
struct options *options;

/* */
struct memory_page {    
    int writer;   /* The nid of the node with writer permissions (-1 if no writer) */
    int *reader; /* Indicates if a node has read permissions (1 if so, 0 if not) */
};
struct memory_page sm_page_table[SM_MAX_PAGES];

void *sm_memory_map;   /* A cache of all of the shared memory */
int   sm_node_count;   /* The number of active nodes */
int   sm_sock;         /* The socket used to receive connections */
int   *client_sockets; /* All of the connected client sockets */
pid_t *client_pids;    /* The PIDs of all of the clients */

int initialize();
int setup(int argc, char **argv);
int process_arguments(int argc, char **argv);
int process_program(int argc, char **argv, int optind);
int read_hostfile();
int node_start();

#endif