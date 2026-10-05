/*
 * solver_stage3_v2.c
 *
 * Stage 3, step 2: replace recursive IDA* DFS with a fixed-size explicit
 * stack while preserving exactly the same child order and pruning rules.
 * This removes call/return overhead and makes the search control flow easier
 * to translate directly to RV32I assembly.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CUBIES 7u
#define PERMUTATIONS 5040u
#define ORIENTATIONS 729u
#define STATES (PERMUTATIONS * ORIENTATIONS)
#define FACES 3u
#define MOVES 9u
#define MAX_DEPTH 11u
#define P3_STATES 210u
#define PDB_ROW_BYTES 105u
#define PDB_BYTES (ORIENTATIONS * PDB_ROW_BYTES)

#include "stage3_tables.h"

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
} state_t;

static const char *const move_names[MOVES] = {
    "R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"
};

static uint8_t solution[MAX_DEPTH];
static uint64_t search_nodes;

/* 105 = 64 + 32 + 8 + 1.  No general multiply is needed. */
static inline uint32_t pdb_row_offset(uint16_t o)
{
    uint32_t x = o;
    return (x << 6) + (x << 5) + (x << 3) + x;
}

static inline uint8_t pdb3_get(uint8_t p3, uint16_t o)
{
    uint32_t byte_index = pdb_row_offset(o) + (uint32_t)(p3 >> 1);
    uint8_t byte = pdb3_packed[byte_index];
    uint8_t shift = (uint8_t)((p3 & 1u) << 2); /* 0 or 4 */
    return (uint8_t)((byte >> shift) & 0x0fu);
}

static inline uint8_t heuristic(uint16_t p, uint16_t o)
{
    uint8_t hp = perm_dist[p];
    uint8_t hpdb = pdb3_get(perm3_map[p], o);
    return hp > hpdb ? hp : hpdb;
}

typedef struct {
    uint16_t p;
    uint16_t o;
    uint16_t next_p;
    uint16_t next_o;
    uint8_t depth;
    int8_t previous_face;
    uint8_t face;
    uint8_t turn;
    uint8_t entered;
} dfs_frame_t;

/*
 * Iterative equivalent of the Stage-2 recursive DFS.
 *
 * Each frame remembers exactly where the recursive function would resume:
 *   - face: next face family being explored
 *   - turn: next quarter/half/inverse child within that face
 *   - next_p/next_o: accumulated state for incremental 1/2/3 quarter turns
 *
 * MAX_DEPTH is only 11, so a fixed 12-frame stack is enough.  No heap and no
 * machine call stack are needed for the search itself.
 */
static int dfs_iterative(uint16_t root_p, uint16_t root_o, uint8_t bound)
{
    dfs_frame_t stack[MAX_DEPTH + 1u];
    uint8_t sp = 0u;

    stack[0] = (dfs_frame_t){
        .p = root_p,
        .o = root_o,
        .next_p = root_p,
        .next_o = root_o,
        .depth = 0u,
        .previous_face = -1,
        .face = 0u,
        .turn = 0u,
        .entered = 0u,
    };

    for (;;) {
        dfs_frame_t *f = &stack[sp];

        if (!f->entered) {
            ++search_nodes;
            f->entered = 1u;

            uint8_t h = heuristic(f->p, f->o);
            if ((unsigned)f->depth + h > bound)
                goto pop_frame;
            if (f->p == 0u && f->o == 0u)
                return 1;
            if (f->depth == bound)
                goto pop_frame;

            f->face = 0u;
            f->turn = 0u;
            f->next_p = f->p;
            f->next_o = f->o;
        }

        /* Find the next child in exactly the same order as the recursive
         * version: face 0..2, and within each face quarter/half/inverse. */
        while (f->face < FACES) {
            if ((int8_t)f->face == f->previous_face) {
                ++f->face;
                f->turn = 0u;
                f->next_p = f->p;
                f->next_o = f->o;
                continue;
            }

            if (f->turn < 3u) {
                uint8_t face = f->face;
                uint8_t turn = f->turn;

                f->next_p = permutation[face][f->next_p];
                f->next_o = orientation[face][f->next_o];
                ++f->turn; /* Parent resumes at the following turn. */

                solution[f->depth] =
                    (uint8_t)((face << 1) + face + turn);

                ++sp;
                stack[sp] = (dfs_frame_t){
                    .p = f->next_p,
                    .o = f->next_o,
                    .next_p = f->next_p,
                    .next_o = f->next_o,
                    .depth = (uint8_t)(f->depth + 1u),
                    .previous_face = (int8_t)face,
                    .face = 0u,
                    .turn = 0u,
                    .entered = 0u,
                };
                goto next_iteration;
            }

            ++f->face;
            f->turn = 0u;
            f->next_p = f->p;
            f->next_o = f->o;
        }

pop_frame:
        if (sp == 0u)
            return 0;
        --sp;

next_iteration:
        ;
    }
}

static int solve(uint16_t p, uint16_t o, uint8_t *solution_length)
{
    uint8_t first_bound = heuristic(p, o);
    search_nodes = 0;

    for (uint8_t bound = first_bound; bound <= MAX_DEPTH; ++bound) {
        if (dfs_iterative(p, o, bound)) {
            *solution_length = bound;
            return 1;
        }
    }
    return 0;
}

/* Input conversion is performed once, outside the search hot path. */
static uint32_t rank_state(const state_t *state)
{
    uint32_t p = 0, o = 0;
    for (unsigned i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        for (unsigned j = i + 1u; j < CUBIES; ++j)
            if (state->p[j] < state->p[i])
                ++smaller;
        p = p * (CUBIES - i) + smaller;
    }
    for (unsigned i = 0; i < 6u; ++i)
        o = o * 3u + state->o[i];
    return p * ORIENTATIONS + o;
}

static int valid(const state_t *state)
{
    unsigned sum = 0;
    for (unsigned i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3u)
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (state->p[j] == state->p[i])
                return 0;
        sum += state->o[i];
    }
    return sum % 3u == 0u;
}

static int parse_state(const char *input, state_t *state)
{
    if (strlen(input) != 14u)
        return 0;
    for (unsigned i = 0; i < 14u; ++i) {
        int limit = i < 7u ? 7 : 3;
        if (input[i] < '1' || input[i] > '0' + limit)
            return 0;
        if (i < 7u)
            state->p[i] = (uint8_t)(input[i] - '1');
        else
            state->o[i - 7u] = (uint8_t)(input[i] - '1');
    }
    return valid(state);
}

int main(int argc, char **argv)
{
    int show_stats = 0;
    const char *input = NULL;

    if (argc == 2) {
        input = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--stats") == 0) {
        show_stats = 1;
        input = argv[2];
    } else {
        fprintf(stderr, "usage: %s [--stats] PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver_stage3_v2");
        return 2;
    }

    state_t state;
    if (!parse_state(input, &state)) {
        fprintf(stderr, "invalid cube state: %s\n", input);
        return 2;
    }

    uint32_t rank = rank_state(&state);
    uint16_t p = (uint16_t)(rank / ORIENTATIONS);
    uint16_t o = (uint16_t)(rank % ORIENTATIONS);
    uint8_t length = 0;

    if (!solve(p, o, &length)) {
        fputs("search failed\n", stderr);
        return 1;
    }

    for (uint8_t i = 0; i < length; ++i) {
        if (i) putchar(' ');
        fputs(move_names[solution[i]], stdout);
    }
    putchar('\n');

    if (show_stats) {
        fprintf(stderr, "solution_length=%u\n", length);
        fprintf(stderr, "root_heuristic=%u\n", heuristic(p, o));
        fprintf(stderr, "search_nodes=%" PRIu64 "\n", search_nodes);
    }
    return 0;
}
