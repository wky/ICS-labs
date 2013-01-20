/*AndrewID guest511
 *Weikun Yang
 *wkyjyy@gmail.com
 * */

/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"
#include "contracts.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. The REQUIRES and ENSURES from 15-122 are included
 *     for your convenience. They can be removed if you like.
 */
char transpose_submit_desc[] = "Transpose submission";

/* using 12 vars */
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, k, a0, a1, a2, a3, a4, a5, a6, a7;
    int *ptr;
    REQUIRES(M > 0);
    REQUIRES(N > 0);
/*
 * perform transpose on 8*8 blocks for 61*67
 */
    if (M == 61 && N == 67){
        for (i = 0; i < 67; i += 8)
        for (j = 0; j < 61; j += 8) 
            for (a1 = 0; a1 + j < 61 && a1 < 8; a1++)
            for (a0 = 0; i + a0 < 67 && a0 < 8; a0++)
                B[a1+j][a0+i] = A[a0+i][a1+j];
    }
/*
 * perform transpose on 8*8 blocks for 64*64
 * in each block, transpose two 4*8 sub-blocks
 * in the order below(0-f):
 *from
 * 0000ffff
 * 1111eeee
 * 2222dddd
 * 3333cccc
 * 4444bbbb
 * 5555aaaa
 * 66669999
 * 77778888
 *to
 * 01234567
 * 01234567
 * 01234567
 * 01234567
 * fedcba98
 * fedcba98
 * fedcba98
 * fedcba98
 */    
    if (M == 64 && N == 64){
    for (j = 0; j < 64; j += 8)
    for (i = 0; i < 64; i += 8){
        /* 0 to 7 */
        for (k = 0; k < 8; k++)
        {
            ptr = &A[i+k][j];
            a0 = ptr[0];
            a1 = ptr[1];
            a2 = ptr[2];
            a3 = ptr[3];
            /* saving 'f's*/
            if (!k){
                a4 = ptr[4];
                a5 = ptr[5];
                a6 = ptr[6];
                a7 = ptr[7];
            }
            ptr = &B[j][i+k];
            ptr[0] = a0;
            ptr[64] = a1;
            ptr[128] = a2;
            ptr[192] = a3;
        }
        /* 8 to e */
        for (k = 7; k > 0; k--)
        {
            ptr = &A[i+k][j+4];
            a0 = ptr[0];
            a1 = ptr[1];
            a2 = ptr[2];
            a3 = ptr[3];
            ptr = &B[j+4][i+k];
            ptr[0] = a0;
            ptr[64] = a1;
            ptr[128] = a2;
            ptr[192] = a3;
        }
        /* f */
        ptr = &B[j+4][i];
        ptr[0] = a4;
        ptr[64] = a5;
        ptr[128] = a6;
        ptr[192] = a7;
    }
    }
/*
 * perform transpose on 8*8 blocks for 32*32
 * using 8 locals to store one row
 */
    if (M == 32 && N == 32) 
    for (i = 0; i < 32; i+=8)
    for (j = 0; j < 32; j+=8)
    for (k = 0; k < 8; k++){
        ptr = &A[i+k][j];
        a0 = ptr[0];
        a1 = ptr[1];
        a2 = ptr[2];
        a3 = ptr[3];
        a4 = ptr[4];
        a5 = ptr[5];
        a6 = ptr[6];
        a7 = ptr[7];
        ptr = &B[j][i+k];
        ptr[0] = a0;
        ptr[32] = a1;
        ptr[64] = a2;
        ptr[96] = a3;
        ptr[128] = a4;
        ptr[160] = a5;
        ptr[192] = a6;
        ptr[224] = a7;
    }
    ENSURES(is_transpose(M, N, A, B));
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
	/* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
			if (A[i][j] != B[j][i]) {
				return 0;
			}
		}
    }
    return 1;
}

