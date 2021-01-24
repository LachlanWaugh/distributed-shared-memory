#include <stdio.h>
#include <stdlib.h>

#ifndef _SM_SETUP_H
#define _SM_SETUP_H

int setup(int argc, char **argv);
int options_init();
int process_arguments(int argc, char **argv);
int process_program(int argc, char **argv, int optind);
int read_hostfile();
int initialize();
int node_start();

#endif