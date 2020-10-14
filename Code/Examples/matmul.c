/*  DSM Assignment: matrix multiplication
 *
 *  Author : Manuel M T Chakravarty
 *  Created: 1 April 2000
 *
 *  Version $Revision: 1.5 $ from $Date: 2003/03/30 14:31:21 $
 *
 *  Copyright (c) [2000..2005] MMT Chakravarty & University of New South Wales
 *
 *  DESCRIPTION ---------------------------------------------------------------
 *
 *  Parallel multiplication of two randomly generated matrices with 
 *  concistency check by a sequential algorithm.
 *
 *  DOCU ----------------------------------------------------------------------
 *
 *  language: ANSI C
 *
 *  This is a moderately naive version of matrix multiplication.  It avoids 
 *  excessive write conflicts in shared memory, but incurs a high 
 *  processor-cache miss rate.  More precisely, it stores matricies row-major
 *  and when computing
 *
 *    C = A * B
 *
 *  it writes C row-wise by assigning the evaluation of a subset of C's rows
 *  to each node process.  However, as each node process accesses B column-
 *  wise, the processor cache is not properly utilised.
 *
 *  TODO ----------------------------------------------------------------------
 *
 */

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include "sm.h"


/* root process
 */
#define ROOT 0

#define EPSILON 1e-10

#define PASSED_TIME(START,END)	(((END).tv_sec - (START).tv_sec) +            \
				 ((END).tv_usec - (START).tv_usec) * 1e-6)

/* number of node process & node process identifier
 */
int nodes, nid;


/* generate a square matrix of the given size randomly (this is done in 
 * parallel)
 */
void generate_matrix (double mat[], int size)
{
  int elemsPerNode = (size * size + nodes - 1) / nodes;
  int elemsForLast = elemsPerNode - (elemsPerNode * nodes - size * size);
  int i;

  for (i = 0; i < ((nid == nodes - 1) ? elemsForLast : elemsPerNode); i++)
    mat[nid * elemsPerNode + i] = 10.0 * rand () / (RAND_MAX + 1.0);
}

/* multiply the second two arguments and store the result in the first argument
 */
void mat_mul (double *matC, double *matA, double *matB, int size)
{
  int    rowsPerNode = size / nodes;
  int    rowsForLast = rowsPerNode - (rowsPerNode * nodes - size);
  int    i, j, k;
  double x;
  
  for (j = nid * rowsPerNode; 
       j < ((nid == nodes - 1)
	    ? nid * rowsPerNode + rowsForLast /*last node gets remaining rows*/
	    : (nid + 1) * rowsPerNode);	      /* others do equal shares */
       j++)
    for (i = 0; i < size; i++) {

      x = 0.0;
      for (k = 0; k < size; k++)
	x += matA[j * size + k] * matB[k * size + i];
      matC[j * size + i] = x;

    }
}

/* sequentially check whether the first argument is the product of the second
 * two arguments (also uses a different traversal order as the actual 
 * multiplication routine)
 */
int is_mat_mul (double *matC, double *matA, double *matB, int size)
{
  int    i, j, k;
  double x;

  for (i = 0; i < size; i++)
    for (j = 0; j < size; j++) {

      x = 0.0;
      for (k = 0; k < size; k++)
	x += matA[j * size + k] * matB[k * size + i];
      if (fabs (matC[j * size + i] - x) > EPSILON) {	/* error */
	return 0;
      }

    }
  return -1;
}

/* output a matrix
 */
void dump_matrix (double *mat, int size)
{
  int i, j;

  for (i = 0; i < size; i++) {
    printf ("|");
    for (j = 0; j < size; j++)
      printf ("%.2f ", mat[i * size + j]);
    printf ("\n");
  }
}

/* quit due to an error
 */
void quit (char *msg)
{
  printf (msg);
  sm_node_exit ();
  exit (1);
}

/* fatal error, no deinit
 */
void fatal (char *msg)
{
  printf ("node %d: Fatal internal error:\n%s\n", nid, msg);
  exit (1);
}

int main (int argc, char *argv[])
{
  int            size, isRoot;
  double        *matA, *matB, *matC;
  struct rusage  start, end;

  /* initialisation and argument processing
   */
  if (sm_node_init (&argc, &argv, &nodes, &nid))
    fatal ("matmul: Cannot initialise!");
  isRoot = (ROOT == nid);
  if (argc != 2)
    quit ("USAGE: matmul SIZE\n");
  sscanf (argv[1], "%d", &size);
  if (size < nid)
    quit ("matmul: SIZE must be at least number of nodes\n");

  /* matrix allocation and generation
   */
  if (isRoot) {
    matA = sm_malloc (sizeof (double) * size * size);
    matB = sm_malloc (sizeof (double) * size * size);
    matC = sm_malloc (sizeof (double) * size * size);
  }
  sm_bcast ((void **) &matA, ROOT);
  sm_bcast ((void **) &matB, ROOT);
  sm_bcast ((void **) &matC, ROOT);
  srand (nid * 100);				/* to get different streams */
  generate_matrix (matA, size);
  generate_matrix (matB, size);
  sm_barrier ();

  /* parallel matrix multiplication
   */
  if (isRoot)
    getrusage (RUSAGE_SELF, &start);
  mat_mul (matC, matA, matB, size);
  sm_barrier ();
  if (isRoot) {
    getrusage (RUSAGE_SELF, &end);
    printf ("cpu usage for process #0: %.2fu+%.2fs\n", 
	    PASSED_TIME (start.ru_utime, end.ru_utime),
	    PASSED_TIME (start.ru_stime, end.ru_stime));
  }

  /* check result
   */
  if (isRoot) {
    if (is_mat_mul (matC, matA, matB, size))
      printf ("Multiplication was successful.\n");
    else
      printf ("Multiplication was NOT successful!\n");
  }

#ifdef DEBUG
  if (isRoot) {
    printf ("matA = \n");
    dump_matrix (matA, size);
    printf ("matB = \n");
    dump_matrix (matB, size);
    printf ("matC = \n");
    dump_matrix (matC, size);
  }
#endif

  sm_node_exit ();
  return 0;
}