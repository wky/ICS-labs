/*AndrewID guest511
 *Weikun Yang
 *wkyjyy@gmail.com
 * */

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cachelab.h"
#define LEN 100

/* accepts short options with arguments */
const char ac_opt[] = "s:E:b:t:hv";

/* representing cache lines */
struct cline{
/* cache line's label */
    unsigned long long label;
/* last time when this line was updated */
    int timestamp;
/* flag indicates availability */
    char flag;
};

/* global vars */
struct cline *cache;
int s, E, b, t = 0;
int hit = 0, miss = 0, evic = 0;
char tracefile[LEN];
char h = 0, v = 0;

/* parse command-line options using get-opt */
void get_input(int argc, char *argv[]){
    int optc = 0, n;
    while((optc = getopt(argc, argv, ac_opt)) != -1){
        if (optarg != NULL)
            n = atoi(optarg);
        switch (optc) {
            case 's':
                s = n;
                break;
            case 'E':
                E = n;
                break;
            case 'b':
                b = n;
                break;
            case 't':
                strncpy(tracefile, optarg, LEN);
                break;
            case 'v':
                v = 1;
                break;
            case 'h':
                h = 1;
                break;
            default:
                break;
        }
    }
}

/* simulates cache load*/
void load(unsigned long long addr)
{
    /* label */
    unsigned long long label = addr>>b;
    /* index of first cache line in set */
    int idx = E * (label&((1<<s) - 1));
    int i, lru = ++t, empty = -1;
    for (i = 0; i < E; i++)
    {
        /* for a occupied cache line */
        if (cache[idx + i].flag)
        {
            /* check if labels match */
            if (cache[idx + i].label == label)
            {
                cache[idx + i].timestamp = t;
                hit++;
                if (v)
                    printf(" hit");
                return;
            }
            /* otherwise, find least recently updated line */
            if (cache[idx + i].timestamp < lru)
            {
                lru = cache[idx + i].timestamp;
                empty = i;
            }
        }
        /* for an empty line, just use it */
        else
        {
            empty = i;
            lru = t;
            break;
        }
    }
    miss++;
    if (v)
        printf(" miss");
    /* eviction occurs on non-empty lines */
    if (lru != t){
        evic ++;
        if (v)
            printf(" evic");
    }
    /* update */
    cache[idx + empty].flag = 1;
    cache[idx + empty].timestamp = t;
    cache[idx + empty].label = label;
}

/* simulate cache store. simply calls load (this is ok in this application)*/
void store(unsigned long long addr){
    load(addr);
}

/* main routine */
int main(int argc, char *argv[])
{
    FILE *fp;
    char tracebuf[LEN];
    unsigned long add;
    get_input(argc, argv);
    if (v)
        printf("s:%d(%d), E:%d, b:%d(%d)\n", s, 1<<s, E, b, 1<<b);
    /* initialize cache */
    cache = (struct cline*)malloc(sizeof(struct cline) * E * (1<<s));
    memset(cache, 0, sizeof(struct cline) * E * (1<<s));
    
    fp = fopen(tracefile, "r");
    /* read one line from file */
    while (fgets(tracebuf, LEN, fp) != NULL) {
        /* do nothing on instruction load */
        if (tracebuf[0] == 'I'){
            //if (v)
                //printf("-----\n");
            continue;
        }
        /* extract the address */
        sscanf(tracebuf + 2, "%lx", &add);
        if (v)
            printf("%c at 0x%lx", tracebuf[1], add);
        /* three types of operations (actually, nothing but load)*/
        switch (tracebuf[1]) {
            case 'L':
                load(add);
                break;
            case 'S':
                store(add);
                break;
            case 'M':
                load(add);
                store(add);
                break;
            default:
                break;
        }
        if (v)
            putchar('\n');
    }
    /* report the results */
    printSummary(hit, miss, evic);
    /* cleaning up */
    fclose(fp);
    free(cache);
    return 0;
}

