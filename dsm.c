#include <stdio.h>
#include <unistd.h>
// #include "sm.h"

int main (int argc, char** argv) {
    extern char *optarg;
    extern int optind;
    static char usage[] = "Usage: dsm [OPTION]... EXECUTABLE-FILE NODE-OPTION...\n\n  "
                           "-H HOSTFILE list of host names\n  "
                           "-h          this usage message"
                           "-l LOGFILE  log each significant allocator action to LOGFILE\n              "
                           "(e.g., read/write fault, invalidate request)\n  "
                           "-n N        start N node processes\n  "
                           "-v          print version information\n\n"
                           "Starts the allocator, which starts N copies (one copy if -n not given) of\n"
                           "EXECUTABLE-FILE.  The NODE-OPTIONs are passed as arguments to the node\n"
                           "processes.  The hosts on which node processes are started are given in\n"
                           "HOSTFILE, which defaults to `hosts'.  If the file does not exist,\n"
                           "`localhost' is used.";
    printf("%s\n", usage);
    return 0;
}