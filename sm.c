#include "sm.h"

/* Register a node process with the SM allocator.
 *
 * - Returns 0 upon successful completion; otherwise, -1.
 * - Command arguments have to be passed in; all dsm-related arguments are 
 *   removed, such that only the arguments for the user program remain.
 * - The number of node processes and the node identification of the current
 *   node process are returned in `nodes' and `nid', respectively.
 */
int sm_node_init (int *argc, char **argv[], int *nodes, int *nid) {
    // TODO:
    return 0;
}

/* Deregister node process.
 */
void sm_node_exit (void) {
    // TODO:
}

/* Allocate object of `size' byte in SM.
 *
 * - Returns NULL if allocation failed.
 */
void *sm_malloc (size_t size); // MILESTONE 2

/* Barrier synchronisation
 *
 * - Barriers are not guaranteed to work after some node processes have quit.
 */
void sm_barrier (void) {
    // TODO:
}

/* Broadcast an address
 *
 * - The address at `*addr' located in node process `root_nid' is transferred
 *   to the memory area referenced by `addr' on the remaining node processes.
 * - `addr' may not refer to shared memory.
 */
void sm_bcast (void **addr, int root_nid); // MILESTONE 2