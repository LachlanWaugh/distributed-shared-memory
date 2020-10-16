/*  DSM Assignment: Shared memory test
 *
 *  Author : Manuel M T Chakravarty
 *  Created: 25 March 2000
 *
 *  Version $Revision: 1.4 $ from $Date: 2003/03/30 14:31:21 $
 *
 *  Copyright (c) 2000 MMT Chakravarty & University of New South Wales
 *
 *  DESCRIPTION ---------------------------------------------------------------
 *
 *  Test shared use of memory
 *
 *  DOCU ----------------------------------------------------------------------
 *
 *  language: ANSI C
 *
 *  TODO ----------------------------------------------------------------------
 *
 */

#include <stdio.h>
#include "sm.h"


void fatal (int nid, char *msg) {
    printf ("node %d: Fatal internal error:\n%s\n", nid, msg);
    exit (1);
}

int main (int argc, char *argv[]) {
    int   nodes, nid;
    char *sharedChar, *sharedChar2;

    if (sm_node_init (&argc, &argv, &nodes, &nid))
        fatal (nid, "share: Cannot initialise!");

    /* first, node #0 allocates a shared variable and uses it to communicate
    * the letter 'A' to node #1
    */
    if (0 == nid) {
        sharedChar = (char *) sm_malloc (sizeof (char));
        *sharedChar = 'A';
    }
    sm_bcast ((void **) &sharedChar, 0);

    /* Checkpoint A */
    printf ("node %d: 1st shared variable is at %p.\n", nid, sharedChar);
    if (0 != nid)
        printf ("node %d: Value in 1st shared variable is %d\n", nid, *sharedChar);

    sm_node_exit();
    return 0;

    /* Checkpoint B */
    sm_barrier ();

    /* next, node #1 allocates a shared variable and uses it to allow node #0
    * to communicate the letter `B' to it
    */
    if (1 == nid)
        sharedChar2 = (char *) sm_malloc (sizeof (char));
    sm_bcast ((void **) &sharedChar2, 1);
  
    /* Checkpoint C */
    if (0 == nid)
        *sharedChar2 = 'B';
  
    /* Checkpoint D */
    sm_barrier ();	/* make sure node #0 wrote the character */
    if (0 != nid)
        printf ("node %d: Value in 2nd shared variable is %d\n", nid, *sharedChar2);
  
    /* Checkpoint E */
    sm_barrier ();

    sm_node_exit ();
    return 0;
}