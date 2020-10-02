#include <stdio.h>
#include <stdlib.h>
#include <sm.h>

#define HOSTS_MAX       16
#define OPT_MAX         16
#define ARG_LEN_MAX     256
#define NAME_LEN_MAX    256
#define COMMAND_LEN_MAX 256

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

typedef struct metadata {
    char  *exe_file;        /* The name of the executable file */
    int    n_proc;          /* The number of processes running */
    int    n_node_opts;     /* The number of arguments given to the node */
    char **node_opts;       /* A list of the arguments given to the node */
    int    n_hosts;         /* The number of host names */
    char **host_names;      /* The list of hostnames */
    char  *log_file;        /* The name of the log file */
} metadata_t;

#ifndef DSM_H
#define DSM_H

int setup(int argc, char **argv, metadata_t *meta);
int run(metadata_t *meta);
int clean(metadata_t *meta);
int read_hostfile(char *hostfile_name, char ***host_names, int *n_hosts);

#endif