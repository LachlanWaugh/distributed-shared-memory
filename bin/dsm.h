#include <stdlib.h>

#ifndef _DSM_H
#define _DSM_H

#define HOSTS_MAX       16
#define OPT_MAX         16
#define ARG_LEN_MAX     256
#define NAME_LEN_MAX    256
#define COMMAND_LEN_MAX 256
#define MSG_LEN_MAX     256

int run();
int clean();
int fatal(char *message);

#endif