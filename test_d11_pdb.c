/*
 * test_d11.c
 *
 * Exhaustive host-side stress test for all HTM distance-11 states.
 *
 * It reuses the current solver_ida.c implementation in the same translation
 * unit, builds an exact full-state BFS oracle once, then runs the IDA* solver
 * on every state whose exact distance is 11.
 */

#define main solver_pdb_embedded_main
#include "solver_pdb.c"
#undef main

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void rank_to_input(uint32_t rank, char out[15])
{
    state_t s;
    unrank_state(rank, &s);

    for (unsigned i = 0; i < 7u; ++i)
        out[i] = (char)('1' + s.p[i]);

    for (unsigned i = 0; i < 7u; ++i)
        out[7u + i] = (char)('1' + s.o[i]);

    out[14] = '\0';
}

static int verify_solution_coords(uint16_t p, uint16_t o, uint8_t length)
{
    for (uint8_t i = 0; i < length; ++i) {
        uint8_t move = solution[i];
        uint8_t face = (uint8_t)(move / 3u);
        uint8_t turns = (uint8_t)(move % 3u + 1u);

        for (uint8_t t = 0; t < turns; ++t) {
            p = permutation[face][p];
            o = orientation[face][o];
        }
    }

    return p == 0u && o == 0u;
}

static void solution_to_text(uint8_t length, char *buf, size_t buf_size)
{
    size_t used = 0;
    buf[0] = '\0';

    for (uint8_t i = 0; i < length; ++i) {
        const char *name = move_names[solution[i]];
        int written = snprintf(
            buf + used,
            buf_size - used,
            "%s%s",
            i ? " " : "",
            name
        );

        if (written < 0)
            return;

        if ((size_t)written >= buf_size - used) {
            buf[buf_size - 1u] = '\0';
            return;
        }

        used += (size_t)written;
    }
}

static int build_exact_distances(
    uint8_t *distance,
    uint32_t *queue,
    uint32_t histogram[12])
{
    memset(distance, 0xff, STATES);
    memset(histogram, 0, 12u * sizeof histogram[0]);

    uint32_t head = 0;
    uint32_t tail = 1;

    queue[0] = 0;
    distance[0] = 0;

    while (head < tail) {
        uint32_t here = queue[head++];
        uint8_t d = distance[here];

        uint16_t p = (uint16_t)(here / ORIENTATIONS);
        uint16_t o = (uint16_t)(here % ORIENTATIONS);

        for (uint8_t face = 0; face < FACES; ++face) {
            uint16_t np = p;
            uint16_t no = o;

            for (uint8_t turn = 0; turn < 3u; ++turn) {
                np = permutation[face][np];
                no = orientation[face][no];

                uint32_t there =
                    (uint32_t)np * ORIENTATIONS + no;

                if (distance[there] == UINT8_MAX) {
                    uint8_t nd = (uint8_t)(d + 1u);

                    if (nd > MAX_DEPTH) {
                        fprintf(
                            stderr,
                            "oracle found depth > %u at rank %" PRIu32 "\n",
                            MAX_DEPTH,
                            there
                        );
                        return 0;
                    }

                    distance[there] = nd;
                    queue[tail++] = there;
                }
            }
        }
    }

    if (tail != STATES) {
        fprintf(
            stderr,
            "oracle reached only %" PRIu32 " / %u states\n",
            tail,
            (unsigned)STATES
        );
        return 0;
    }

    for (uint32_t rank = 0; rank < STATES; ++rank)
        ++histogram[distance[rank]];

    return 1;
}

int main(void)
{
    build_transitions();
    build_perm3_map();

    if (!build_perm_dist() || !build_pdb3()) {
        fputs("failed to build abstraction tables\n", stderr);
        return 1;
    }

    uint8_t *distance = malloc(STATES);
    uint32_t *queue =
        malloc((size_t)STATES * sizeof queue[0]);

    if (!distance || !queue) {
        fputs("could not allocate native BFS oracle memory\n", stderr);
        free(distance);
        free(queue);
        return 1;
    }

    uint32_t histogram[12];

    puts("Building exact full-state BFS oracle...");

    clock_t bfs_start = clock();

    if (!build_exact_distances(distance, queue, histogram)) {
        free(distance);
        free(queue);
        return 1;
    }

    double bfs_seconds =
        (double)(clock() - bfs_start) / CLOCKS_PER_SEC;

    puts("\nExact distance histogram:");
    for (unsigned d = 0; d <= MAX_DEPTH; ++d)
        printf("  d=%2u : %" PRIu32 "\n", d, histogram[d]);

    printf("\nBFS oracle built in %.3f s\n", bfs_seconds);
    printf("distance-11 states: %" PRIu32 "\n", histogram[11]);

    if (histogram[11] != 2644u) {
        fprintf(
            stderr,
            "WARNING: expected 2644 distance-11 states, got %" PRIu32 "\n",
            histogram[11]
        );
    }

    FILE *csv = fopen("d11_results.csv", "w");
    if (!csv) {
        perror("d11_results.csv");
        free(distance);
        free(queue);
        return 1;
    }

    fputs(
        "rank,state,root_heuristic,search_nodes,solution\n",
        csv
    );

    uint32_t tested = 0;
    uint32_t failures = 0;
    uint64_t total_nodes = 0;
    uint64_t worst_nodes = 0;
    uint32_t worst_rank = 0;
    uint8_t worst_h = 0;
    char worst_state[15] = "";
    char worst_solution[128] = "";

    clock_t search_start = clock();

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        if (distance[rank] != 11u)
            continue;

        ++tested;

        uint16_t p = (uint16_t)(rank / ORIENTATIONS);
        uint16_t o = (uint16_t)(rank % ORIENTATIONS);
        uint8_t h = heuristic(p, o);
        uint8_t length = 0;

        if (!solve(p, o, &length)) {
            fprintf(
                stderr,
                "FAIL rank=%" PRIu32 ": solver returned no solution\n",
                rank
            );
            ++failures;
            continue;
        }

        if (length != 11u) {
            fprintf(
                stderr,
                "FAIL rank=%" PRIu32 ": exact=11 solver=%u\n",
                rank,
                length
            );
            ++failures;
        }

        if (h > 11u) {
            fprintf(
                stderr,
                "FAIL rank=%" PRIu32
                ": inadmissible root heuristic h=%u > 11\n",
                rank,
                h
            );
            ++failures;
        }

        if (!verify_solution_coords(p, o, length)) {
            fprintf(
                stderr,
                "FAIL rank=%" PRIu32
                ": returned path does not solve state\n",
                rank
            );
            ++failures;
        }

        char state_text[15];
        char solution_text[128];

        rank_to_input(rank, state_text);
        solution_to_text(length, solution_text, sizeof solution_text);

        fprintf(
            csv,
            "%" PRIu32 ",%s,%u,%" PRIu64 ",\"%s\"\n",
            rank,
            state_text,
            h,
            search_nodes,
            solution_text
        );

        total_nodes += search_nodes;

        if (search_nodes > worst_nodes) {
            worst_nodes = search_nodes;
            worst_rank = rank;
            worst_h = h;
            strcpy(worst_state, state_text);
            strcpy(worst_solution, solution_text);
        }

        if (tested % 100u == 0u) {
            printf(
                "[%4" PRIu32 "/%" PRIu32
                "] current=%" PRIu64
                " worst=%" PRIu64
                " state=%s\n",
                tested,
                histogram[11],
                search_nodes,
                worst_nodes,
                state_text
            );
            fflush(stdout);
        }
    }

    double search_seconds =
        (double)(clock() - search_start) / CLOCKS_PER_SEC;

    fclose(csv);

    puts("\n========================================");
    puts("Distance-11 exhaustive stress-test");
    puts("========================================");

    printf("tested             : %" PRIu32 "\n", tested);
    printf("failures           : %" PRIu32 "\n", failures);

    if (tested != 0u) {
        printf(
            "average search nodes: %.2f\n",
            (double)total_nodes / tested
        );
    }

    printf("worst search nodes : %" PRIu64 "\n", worst_nodes);
    printf("worst rank         : %" PRIu32 "\n", worst_rank);
    printf("worst state        : %s\n", worst_state);
    printf("worst root h       : %u\n", worst_h);
    printf("worst solution     : %s\n", worst_solution);
    printf("search test time   : %.3f s\n", search_seconds);
    puts("CSV                 : d11_results.csv");

    {
        const char *fixed = "21345671111111";
        state_t s;

        if (!parse_state(fixed, &s)) {
            fputs("internal error parsing fixed vector\n", stderr);
            ++failures;
        } else {
            uint32_t rank = rank_state(&s);
            uint16_t p = (uint16_t)(rank / ORIENTATIONS);
            uint16_t o = (uint16_t)(rank % ORIENTATIONS);
            uint8_t length = 0;

            if (!solve(p, o, &length)) {
                fputs("fixed vector search failed\n", stderr);
                ++failures;
            } else {
                printf("\nFixed vector %s\n", fixed);
                printf("  exact distance : %u\n", distance[rank]);
                printf("  solution length: %u\n", length);
                printf("  root heuristic : %u\n", heuristic(p, o));
                printf("  search nodes   : %" PRIu64 "\n", search_nodes);
            }
        }
    }

    free(distance);
    free(queue);

    return failures == 0u ? 0 : 1;
}
