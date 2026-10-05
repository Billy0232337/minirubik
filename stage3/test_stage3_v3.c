#define main solver_stage3_v3_embedded_main
#include "solver_stage3_v3.c"
#undef main

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STATES_V3 (PERMUTATIONS * ORIENTATIONS)

static int build_exact_full_dist(uint8_t *dist, uint32_t *queue)
{
    memset(dist, 0xff, STATES_V3);
    uint32_t head = 0, tail = 1;
    queue[0] = 0;
    dist[0] = 0;
    while (head < tail) {
        uint32_t here = queue[head++];
        uint8_t d = dist[here];
        uint16_t p = (uint16_t)(here / ORIENTATIONS);
        uint16_t o = (uint16_t)(here % ORIENTATIONS);
        for (uint8_t face = 0; face < FACES; ++face) {
            uint16_t np = p, no = o;
            for (uint8_t turn = 0; turn < 3u; ++turn) {
                np = permutation[face][np];
                no = orientation[face][no];
                uint32_t there = (uint32_t)np * ORIENTATIONS + no;
                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t)(d + 1u);
                    queue[tail++] = there;
                }
            }
        }
    }
    return tail == STATES_V3;
}

static int verify_path(uint16_t p, uint16_t o, uint8_t len)
{
    for (uint8_t i=0;i<len;++i) {
        uint8_t move=solution[i];
        uint8_t face=(uint8_t)(move/3u);
        uint8_t turns=(uint8_t)(move%3u+1u);
        for(uint8_t t=0;t<turns;++t){
            p=permutation[face][p];
            o=orientation[face][o];
        }
    }
    return p==0u && o==0u;
}

int main(void)
{
    uint8_t *dist = malloc(STATES_V3);
    uint32_t *queue = malloc((size_t)STATES_V3 * sizeof(*queue));
    if(!dist || !queue) return 2;
    if(!build_exact_full_dist(dist, queue)) return 2;
    free(queue);

    uint32_t mismatches=0, invalid=0;
    uint32_t worst_nodes=0, worst_rank=0;
    uint64_t total_nodes=0;
    clock_t start=clock();
    for(uint32_t rank=0; rank<STATES_V3; ++rank){
        uint16_t p=(uint16_t)(rank/ORIENTATIONS);
        uint16_t o=(uint16_t)(rank%ORIENTATIONS);
        uint8_t len=0xff;
        if(!solve(p,o,&len) || len!=dist[rank]) ++mismatches;
        else if(!verify_path(p,o,len)) ++invalid;
        total_nodes += search_nodes;
        if(search_nodes>worst_nodes){ worst_nodes=search_nodes; worst_rank=rank; }
        if ((rank+1u)%500000u==0u)
            fprintf(stderr,"progress %u/%u mismatches=%u invalid=%u\n",rank+1u,(unsigned)STATES_V3,mismatches,invalid);
    }
    double secs=(double)(clock()-start)/CLOCKS_PER_SEC;
    printf("states=%u\nmismatches=%u\ninvalid=%u\navg_nodes=%.2f\nworst_nodes=%u\nworst_rank=%u\ncpu=%.3f s\n",
      (unsigned)STATES_V3,mismatches,invalid,(double)total_nodes/STATES_V3,worst_nodes,worst_rank,secs);
    free(dist);
    return (mismatches||invalid)?1:0;
}
