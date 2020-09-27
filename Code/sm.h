/*  DSM Assignment: Shared memory library - API
 *
 *  Author : Manuel M. T. Chakravarty
 *  Created: 16 March 2000
 *
 *  Version $Revision: 1.2 $ from $Date: 2003/04/11 08:45:04 $
 *
 *  Copyright (c) [2000..2001] MMT Chakravarty & University of New South Wales
 *
 *  DESCRIPTION ---------------------------------------------------------------
 *
 *  This header defines the shared memory API for node processes.
 *
 *  DOCU ----------------------------------------------------------------------
 *
 *  language: ANSI C header
 *
 *  TODO ----------------------------------------------------------------------
 *
 */

#ifndef	_SM_H
#define	_SM_H

#include <stdlib.h>

/* Register a node process with the SM allocator.
 *
 * - Returns 0 upon successful completion; otherwise, -1.
 * - Command arguments have to be passed in; all dsm-related arguments are 
 *   removed, such that only the arguments for the user program remain.
 * - The number of node processes and the node identification of the current
 *   node process are returned in `nodes' and `nid', respectively.
 */
int sm_node_init (int *argc, char **argv[], int *nodes, int *nid);

/* Deregister node process.
 */
void sm_node_exit (void);

/* Allocate object of `size' byte in SM.
 *
 * - Returns NULL if allocation failed.
 */
void *sm_malloc (size_t size);

/* Barrier synchronisation
 *
 * - Barriers are not guaranteed to work after some node processes have quit.
 */
void sm_barrier (void);

/* Broadcast an address
 *
 * - The address at `*addr' located in node process `root_nid' is transferred
 *   to the memory area referenced by `addr' on the remaining node processes.
 * - `addr' may not refer to shared memory.
 */
void sm_bcast (void **addr, int root_nid);


#endif /* !_SM_H  */