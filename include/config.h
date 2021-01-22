
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

#define HOSTS_MAX       16
#define OPT_MAX         16
#define ARG_LEN_MAX     256
#define NAME_LEN_MAX    256
#define COMMAND_LEN_MAX 256
#define MSG_LEN_MAX     256
#define SM_MAX_NODES    16

#define SM_LEN_MAX 128
#define SM_ARG_MAX 32
#define SM_HOSTS_MAX 10
#define SM_PAGESIZE  1024
#define SM_MAX_PAGES 1000
#define SM_PORT      9243

#define ANSI_COLOR_RED   "\x1b[31m"
#define ANSI_COLOR_RESET "\x1b[0m"

/* */
struct options {
    int    n_nodes;    /* The number of nodes required */   
    FILE  *log_file;   /* The log file for the operations performed */
    
    char **host_names; /* A list of host-names (addresses of the nodes) */
    int    n_hosts;    /* The number of unique hosts (for when there are more hosts than nodes) */

    char  *program;    /* The name of the program to be ran */
    char **prog_args;  /* The arguments to be passed to the program */
};
struct options *options;

/* */
struct memory_page {    
    int  writer;  /* The nid of the node with writer permissions (-1 if no writer) */
    int *readers; /* Indicates if a node has read permissions (1 if so, 0 if not) */
};
struct memory_page sm_page_table[SM_MAX_PAGES];

void *sm_memory_map;                /* A cache of all of the shared memory */
int   sm_node_count;                /* The number of active nodes */
int   sm_socket;                    /* The socket used to receive connections */
int   client_sockets[SM_MAX_NODES]; /* All of the connected client sockets */
pid_t client_pids[SM_MAX_NODES];    /* The PIDs of all of the clients */