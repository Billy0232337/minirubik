/*
 * test_stage2.c
 *
 * Host-side validation tool for Stage 2 correctness gates H1-H4.
 *
 * Build next to solver_pdb.c:
 *   gcc -O2 -std=c11 -Wall -Wextra -pedantic test_stage2.c -o test_stage2
 *
 * Run fast gates:
 *   ./test_stage2 --fast
 *
 * Run exhaustive H3 (may take minutes):
 *   ./test_stage2 --h3
 *
 * Run everything:
 *   ./test_stage2 --all
 *
 * This is a HOST-ONLY validation tool. It intentionally builds the complete
 * BFS oracle and an unpacked PDB reference; neither belongs in the RV32I target.
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

static double seconds_since(clock_t start)
{
    return (double)(clock() - start) / (double)CLOCKS_PER_SEC;
}

static int build_exact_full_dist(uint8_t *dist, uint32_t *queue,
                                 uint32_t histogram[MAX_DEPTH + 1u])
{
    memset(dist, 0xff, STATES);
    memset(histogram, 0, (MAX_DEPTH + 1u) * sizeof histogram[0]);

    uint32_t head = 0;
    uint32_t tail = 1;
    queue[0] = 0;
    dist[0] = 0;

    while (head < tail) {
        uint32_t here = queue[head++];
        uint8_t d = dist[here];
        uint16_t p = (uint16_t)(here / ORIENTATIONS);
        uint16_t o = (uint16_t)(here % ORIENTATIONS);

        for (uint8_t face = 0; face < FACES; ++face) {
            uint16_t np = p;
            uint16_t no = o;

            for (uint8_t turn = 0; turn < 3u; ++turn) {
                np = permutation[face][np];
                no = orientation[face][no];
                uint32_t there = (uint32_t)np * ORIENTATIONS + no;

                if (dist[there] == UINT8_MAX) {
                    uint8_t nd = (uint8_t)(d + 1u);
                    if (nd > MAX_DEPTH) {
                        fprintf(stderr,
                                "oracle discovered depth %u > %u at rank %" PRIu32 "\n",
                                nd, MAX_DEPTH, there);
                        return 0;
                    }
                    dist[there] = nd;
                    queue[tail++] = there;
                }
            }
        }
    }

    if (tail != STATES) {
        fprintf(stderr, "oracle reached only %" PRIu32 " / %u states\n",
                tail, (unsigned)STATES);
        return 0;
    }

    for (uint32_t rank = 0; rank < STATES; ++rank)
        ++histogram[dist[rank]];

    return 1;
}

/* Build an independent unpacked reference for the 3-cubie + orientation PDB. */
static int build_unpacked_pdb_reference(uint8_t *dist)
{
    uint8_t p3_move[FACES][P3_STATES];
    uint8_t new_position[FACES][CUBIES];

    for (uint8_t face = 0; face < FACES; ++face) {
        for (uint8_t destination = 0; destination < CUBIES; ++destination)
            new_position[face][source[face][destination]] = destination;
    }

    for (uint8_t face = 0; face < FACES; ++face) {
        for (unsigned p3 = 0; p3 < P3_STATES; ++p3) {
            uint8_t p0, p1, p2;
            unrank_perm3_positions((uint8_t)p3, &p0, &p1, &p2);
            p3_move[face][p3] = rank_perm3_positions(
                new_position[face][p0],
                new_position[face][p1],
                new_position[face][p2]);
        }
    }

    const uint32_t count = (uint32_t)P3_STATES * ORIENTATIONS;
    uint32_t *queue = malloc((size_t)count * sizeof queue[0]);
    if (!queue)
        return 0;

    memset(dist, 0xff, count);
    uint32_t head = 0;
    uint32_t tail = 1;
    queue[0] = 0;
    dist[0] = 0;

    while (head < tail) {
        uint32_t here = queue[head++];
        uint16_t o = (uint16_t)(here / P3_STATES);
        uint8_t p3 = (uint8_t)(here % P3_STATES);

        for (uint8_t face = 0; face < FACES; ++face) {
            uint16_t no = o;
            uint8_t np3 = p3;

            for (uint8_t turn = 0; turn < 3u; ++turn) {
                no = orientation[face][no];
                np3 = p3_move[face][np3];
                uint32_t there = (uint32_t)no * P3_STATES + np3;

                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t)(dist[here] + 1u);
                    queue[tail++] = there;
                }
            }
        }
    }

    free(queue);
    return tail == count;
}

static int verify_returned_path(uint16_t p, uint16_t o, uint8_t length)
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

static int run_h2_h4(const uint8_t *pdb_ref)
{
    unsigned failures = 0;

    uint8_t perm_max = 0;
    unsigned perm_unfilled = 0;
    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        if (perm_dist[p] == UINT8_MAX)
            ++perm_unfilled;
        else if (perm_dist[p] > perm_max)
            perm_max = perm_dist[p];
    }

    uint8_t pdb_max = 0;
    unsigned pdb_unfilled = 0;
    uint32_t packed_mismatch = 0;
    uint32_t even_checked = 0;
    uint32_t odd_checked = 0;

    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        for (unsigned p3 = 0; p3 < P3_STATES; ++p3) {
            size_t index = (size_t)o * P3_STATES + p3;
            uint8_t ref = pdb_ref[index];
            uint8_t packed = pdb3_get((uint8_t)p3, o);

            if (ref == UINT8_MAX)
                ++pdb_unfilled;
            else if (ref > pdb_max)
                pdb_max = ref;

            if (packed != ref) {
                if (packed_mismatch < 10u) {
                    fprintf(stderr,
                            "H4 mismatch: o=%u p3=%u ref=%u packed=%u\n",
                            o, p3, ref, packed);
                }
                ++packed_mismatch;
            }

            if (p3 & 1u)
                ++odd_checked;
            else
                ++even_checked;
        }
    }

    puts("\nH2: table population / solved entry / maximum");
    printf("  perm entries       : %u\n", (unsigned)PERMUTATIONS);
    printf("  perm unfilled      : %u\n", perm_unfilled);
    printf("  perm solved entry  : %u\n", perm_dist[0]);
    printf("  perm maximum       : %u\n", perm_max);
    printf("  PDB entries        : %u\n", (unsigned)(P3_STATES * ORIENTATIONS));
    printf("  PDB unfilled       : %u\n", pdb_unfilled);
    printf("  PDB solved entry   : %u\n", pdb3_get(0, 0));
    printf("  PDB maximum        : %u\n", pdb_max);

    if (perm_unfilled != 0u || perm_dist[0] != 0u ||
        pdb_unfilled != 0u || pdb3_get(0, 0) != 0u ||
        perm_max > MAX_DEPTH || pdb_max > MAX_DEPTH) {
        ++failures;
        puts("  H2 RESULT          : FAIL");
    } else {
        puts("  H2 RESULT          : PASS");
    }

    puts("\nH4: packed accessor vs unpacked reference");
    printf("  even indices tested: %" PRIu32 "\n", even_checked);
    printf("  odd indices tested : %" PRIu32 "\n", odd_checked);
    printf("  mismatches         : %" PRIu32 "\n", packed_mismatch);

    if (packed_mismatch != 0u) {
        ++failures;
        puts("  H4 RESULT          : FAIL");
    } else {
        puts("  H4 RESULT          : PASS");
    }

    return failures == 0u;
}

static int run_h1(const uint8_t *exact)
{
    uint32_t violations = 0;
    uint8_t largest_slack = 0;
    uint32_t tight = 0;

    clock_t start = clock();

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint16_t p = (uint16_t)(rank / ORIENTATIONS);
        uint16_t o = (uint16_t)(rank % ORIENTATIONS);
        uint8_t h = heuristic(p, o);
        uint8_t d = exact[rank];

        if (h > d) {
            if (violations < 10u) {
                fprintf(stderr,
                        "H1 violation rank=%" PRIu32 " h=%u d=%u\n",
                        rank, h, d);
            }
            ++violations;
        }

        if (h == d)
            ++tight;
        else if (d >= h && (uint8_t)(d - h) > largest_slack)
            largest_slack = (uint8_t)(d - h);
    }

    puts("\nH1: heuristic admissibility over complete domain");
    printf("  states checked     : %u\n", (unsigned)STATES);
    printf("  violations         : %" PRIu32 "\n", violations);
    printf("  exact/tight h      : %" PRIu32 "\n", tight);
    printf("  largest d-h        : %u\n", largest_slack);
    printf("  wall CPU time      : %.3f s\n", seconds_since(start));
    printf("  H1 RESULT          : %s\n", violations == 0u ? "PASS" : "FAIL");

    return violations == 0u;
}

static int run_h3(const uint8_t *exact)
{
    uint32_t mismatches = 0;
    uint32_t bad_paths = 0;
    uint64_t total_nodes = 0;
    uint64_t worst_nodes = 0;
    uint32_t worst_rank = 0;

    clock_t start = clock();

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint16_t p = (uint16_t)(rank / ORIENTATIONS);
        uint16_t o = (uint16_t)(rank % ORIENTATIONS);
        uint8_t length = 0;

        if (!solve(p, o, &length)) {
            if (mismatches < 10u)
                fprintf(stderr, "H3 no solution at rank=%" PRIu32 "\n", rank);
            ++mismatches;
            continue;
        }

        if (length != exact[rank]) {
            if (mismatches < 10u) {
                fprintf(stderr,
                        "H3 mismatch rank=%" PRIu32 " expected=%u got=%u\n",
                        rank, exact[rank], length);
            }
            ++mismatches;
        }

        if (!verify_returned_path(p, o, length)) {
            if (bad_paths < 10u)
                fprintf(stderr, "H3 bad path at rank=%" PRIu32 "\n", rank);
            ++bad_paths;
        }

        total_nodes += search_nodes;
        if (search_nodes > worst_nodes) {
            worst_nodes = search_nodes;
            worst_rank = rank;
        }

        if ((rank + 1u) % 100000u == 0u) {
            double elapsed = seconds_since(start);
            printf("  H3 progress %7" PRIu32 "/%u  elapsed=%.1f s  mismatches=%" PRIu32 "\n",
                   rank + 1u, (unsigned)STATES, elapsed, mismatches);
            fflush(stdout);
        }
    }

    puts("\nH3: end-to-end optimal solution length over complete domain");
    printf("  states checked     : %u\n", (unsigned)STATES);
    printf("  length mismatches  : %" PRIu32 "\n", mismatches);
    printf("  invalid paths      : %" PRIu32 "\n", bad_paths);
    printf("  average nodes      : %.2f\n", (double)total_nodes / STATES);
    printf("  worst nodes        : %" PRIu64 "\n", worst_nodes);
    printf("  worst rank         : %" PRIu32 "\n", worst_rank);
    printf("  wall CPU time      : %.3f s\n", seconds_since(start));
    printf("  H3 RESULT          : %s\n",
           (mismatches == 0u && bad_paths == 0u) ? "PASS" : "FAIL");

    return mismatches == 0u && bad_paths == 0u;
}

static void print_histogram(const uint32_t histogram[MAX_DEPTH + 1u])
{
    puts("\nExact BFS distance histogram:");
    for (unsigned d = 0; d <= MAX_DEPTH; ++d)
        printf("  d=%2u : %" PRIu32 "\n", d, histogram[d]);
}

int main(int argc, char **argv)
{
    int want_fast = 0;
    int want_h3 = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s --fast | --h3 | --all\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "--fast") == 0)
        want_fast = 1;
    else if (strcmp(argv[1], "--h3") == 0)
        want_h3 = 1;
    else if (strcmp(argv[1], "--all") == 0) {
        want_fast = 1;
        want_h3 = 1;
    } else {
        fprintf(stderr, "unknown option: %s\n", argv[1]);
        return 2;
    }

    puts("Building solver tables...");
    build_transitions();
    build_perm3_map();
    if (!build_perm_dist() || !build_pdb3()) {
        fputs("failed to build solver tables\n", stderr);
        return 1;
    }

    uint8_t *exact = malloc(STATES);
    uint32_t *queue = malloc((size_t)STATES * sizeof queue[0]);
    if (!exact || !queue) {
        fputs("failed to allocate full BFS oracle\n", stderr);
        free(exact);
        free(queue);
        return 1;
    }

    uint32_t histogram[MAX_DEPTH + 1u];
    puts("Building exact full-state BFS oracle...");
    clock_t oracle_start = clock();
    if (!build_exact_full_dist(exact, queue, histogram)) {
        free(exact);
        free(queue);
        return 1;
    }
    printf("Oracle built in %.3f s\n", seconds_since(oracle_start));
    print_histogram(histogram);

    int ok = 1;

    if (want_fast) {
        const size_t pdb_count = (size_t)P3_STATES * ORIENTATIONS;
        uint8_t *pdb_ref = malloc(pdb_count);
        if (!pdb_ref) {
            fputs("failed to allocate unpacked PDB reference\n", stderr);
            free(exact);
            free(queue);
            return 1;
        }

        if (!build_unpacked_pdb_reference(pdb_ref)) {
            fputs("failed to build unpacked PDB reference\n", stderr);
            free(pdb_ref);
            free(exact);
            free(queue);
            return 1;
        }

        if (!run_h2_h4(pdb_ref))
            ok = 0;
        if (!run_h1(exact))
            ok = 0;

        free(pdb_ref);
    }

    if (want_h3) {
        if (!run_h3(exact))
            ok = 0;
    }

    free(exact);
    free(queue);

    puts("\n========================================");
    printf("OVERALL RESULT: %s\n", ok ? "PASS" : "FAIL");
    puts("========================================");

    return ok ? 0 : 1;
}
