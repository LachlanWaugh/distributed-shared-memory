#include <stdlib.h>

#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

int sm_fatal(char *message);

int allocator_init   ();
int socket_init      ();
int allocator_end    ();
int allocate         ();
int wait_for_messages();

#endif