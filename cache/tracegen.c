/* 
 * tracegen.c - Running the binary tracegen with valgrind produces
 * a memory trace of all of the registered transpose functions. 
 * 
 * The beginning and end of each registered transpose function's trace
 * is indicated by reading from "marker" addresses. These two marker
 * addresses are recorded in file for later use.
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include "cachelab.h"

/* External variables declared in cachelab.c */
extern trans_func_t func_list[MAX_TRANS_FUNCS];
extern int func_counter; 

/* External function from trans.c */
extern void registerFunctions();

/* Markers used to bound trace regions of interest */
volatile char MARKER_START, MARKER_END;

static int A[256][256];
static int B[256][256];
static int M;
static int N;

int main(int argc, char* argv[]){
	int i;

	char c;
	while( (c=getopt(argc,argv,"M:N:")) != -1){
		switch(c){
		case 'M':
			M = atoi(optarg);
			break;
		case 'N':
			N = atoi(optarg);
			break;
		case '?':
		default:
			printf("./tracegen failed to parse its options.\n");
        exit(1);
		}
	}
  

	/*  Register transpose functions */
	registerFunctions();

	/* Fill A with data */
	initMatrix(M,N, A, B); 

	/* Record marker addresses */
	FILE* marker_fp = fopen(".marker","w");
	assert(marker_fp);
	fprintf(marker_fp, "%llx %llx", 
			(unsigned long long int) &MARKER_START,
			(unsigned long long int) &MARKER_END );
	fclose(marker_fp);

	/* Invoke registered transpose functions */
	for (i=0; i < func_counter; i++) {
		/*fprintf(stderr, "func %d (%s)\n",  i+1, func_list[i].description);
		  fflush(stderr);*/
		MARKER_START = 33;
		(*func_list[i].func_ptr)(M, N, A, B);
		MARKER_END = 34;
	}
	// printf("tracegen: %llx %llx\n", A, B);
	return 0;
}


