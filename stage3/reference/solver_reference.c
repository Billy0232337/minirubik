/*
 * solver_stage3_v3.c
 *
 * Stage 3, final C pass: keep the same IDA* algorithm, but reduce hot-path
 * memory traffic and arithmetic before translating the search to RV32I.
 *
 * Main changes from v2:
 *   1. Combine perm_dist[p] and perm3_map[p] into one 16-bit perm_meta load.
 *   2. Precompute PDB row byte offsets, replacing o*105 shift/add arithmetic.
 *   3. Remove the per-frame depth field: explicit-stack index == DFS depth.
 *   4. Remove the per-frame entered flag by evaluating children before push.
 *   5. Use a 32-bit node counter; the complete search is far below 2^32 nodes.
 *   6. Avoid rank/729 and rank%729 in input conversion by computing p/o directly.
 */
#include <stdint.h>

#define CUBIES 7u
#define PERMUTATIONS 5040u
#define ORIENTATIONS 729u
#define FACES 3u
#define MOVES 9u
#define MAX_DEPTH 11u

#include "../stage3_tables_v3.h"

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
} state_t;

static const char *const move_names[MOVES] = {
    "R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"
};

static uint8_t solution[MAX_DEPTH];
static uint32_t search_nodes;

/*
 * perm_meta[p]:
 *   bits 15..8 = exact full-permutation abstraction distance
 *   bits  7..0 = three-cubie abstraction rank (0..209)
 *
 * One halfword load replaces two independent byte loads in v2.
 */
static inline uint8_t heuristic(uint16_t p, uint16_t o)
{
    uint16_t meta = perm_meta[p];
    uint8_t p3 = (uint8_t)meta;
    uint8_t hp = (uint8_t)(meta >> 8);

    uint32_t byte_index = pdb_row_offset[o] + (uint32_t)(p3 >> 1);
    uint8_t packed = pdb3_packed[byte_index];
    uint8_t shift = (uint8_t)((p3 & 1u) << 2);
    uint8_t hpdb = (uint8_t)((packed >> shift) & 0x0fu);

    return hp > hpdb ? hp : hpdb;
}

/*
 * Resume state for one active DFS level.
 *
 * The explicit stack index is the depth, so v2's depth byte is unnecessary.
 * The node is evaluated before it is pushed, so v2's entered byte is also
 * unnecessary.
 */
typedef struct {
    uint16_t p;
    uint16_t o;
    uint16_t next_p;
    uint16_t next_o;
    int8_t previous_face;
    uint8_t face;
    uint8_t turn;
} dfs_frame_t;

static inline void init_frame(dfs_frame_t *f, uint16_t p, uint16_t o,
                              int8_t previous_face)
{
    f->p = p;
    f->o = o;
    f->next_p = p;
    f->next_o = o;
    f->previous_face = previous_face;
    f->face = 0u;
    f->turn = 0u;
}

/*
 * Iterative IDA* depth-first pass for one bound.
 *
 * A generated child is counted and evaluated immediately.  Only children that
 * survive f=g+h pruning and can still expand are pushed onto the explicit
 * stack.  This removes the entered-state branch from every loop iteration.
 */
static int dfs_iterative(uint16_t root_p, uint16_t root_o, uint8_t bound)
{
    dfs_frame_t stack[MAX_DEPTH + 1u];
    uint8_t sp = 0u;

    ++search_nodes;
    if (heuristic(root_p, root_o) > bound)
        return 0;
    if (root_p == 0u && root_o == 0u)
        return 1;
    if (bound == 0u)
        return 0;

    init_frame(&stack[0], root_p, root_o, -1);

    for (;;) {
        dfs_frame_t *f = &stack[sp];

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

                /* Incremental quarter turns produce face, face^2, face^3. */
                f->next_p = permutation[face][f->next_p];
                f->next_o = orientation[face][f->next_o];
                ++f->turn;

                uint16_t child_p = f->next_p;
                uint16_t child_o = f->next_o;
                uint8_t child_depth = (uint8_t)(sp + 1u);

                ++search_nodes;
                uint8_t h = heuristic(child_p, child_o);
                if ((unsigned)child_depth + h > bound)
                    continue;

                solution[sp] = (uint8_t)((face << 1) + face + turn);

                if (child_p == 0u && child_o == 0u)
                    return 1;

                if (child_depth == bound)
                    continue;

                ++sp;
                init_frame(&stack[sp], child_p, child_o, (int8_t)face);
                goto next_iteration;
            }

            ++f->face;
            f->turn = 0u;
            f->next_p = f->p;
            f->next_o = f->o;
        }

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
    search_nodes = 0u;

    for (uint8_t bound = first_bound; bound <= MAX_DEPTH; ++bound) {
        if (dfs_iterative(p, o, bound)) {
            *solution_length = bound;
            return 1;
        }
    }
    return 0;
}

/* Input conversion is outside the search hot path, but avoid division/modulo
 * anyway so this data path also maps cleanly to RV32I. */
static void rank_coordinates(const state_t *state, uint16_t *p_out,
                             uint16_t *o_out)
{
    uint32_t p = 0u;

    for (unsigned i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0u;
        for (unsigned j = i + 1u; j < CUBIES; ++j)
            if (state->p[j] < state->p[i])
                ++smaller;

        /* p = p * (7-i) + smaller, using only shift/add/sub constants. */
        switch (CUBIES - i) {
        case 7u: p = (p << 3) - p; break;
        case 6u: p = (p << 2) + (p << 1); break;
        case 5u: p = (p << 2) + p; break;
        case 4u: p <<= 2; break;
        case 3u: p = (p << 1) + p; break;
        case 2u: p <<= 1; break;
        default: break;
        }
        p += smaller;
    }

    uint32_t o = 0u;
    for (unsigned i = 0; i < 6u; ++i)
        o = (o << 1) + o + state->o[i];

    *p_out = (uint16_t)p;
    *o_out = (uint16_t)o;
}

static int valid(const state_t *state)
{
    uint8_t mod3 = 0u;

    for (unsigned i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3u)
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (state->p[j] == state->p[i])
                return 0;

        mod3 = (uint8_t)(mod3 + state->o[i]);
        if (mod3 >= 3u)
            mod3 = (uint8_t)(mod3 - 3u);
    }

    return mod3 == 0u;
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

static int parse_state(const char *input, state_t *state)
{
    if (str_len(input) != 14u)
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

volatile uint8_t ref_solution_length;
volatile uint8_t ref_root_heuristic;
volatile uint32_t ref_search_nodes;
volatile uint16_t ref_p;
volatile uint16_t ref_o;
volatile uint32_t ref_solution_checksum;

/*
 * The fixed input is intentionally defined in a separate translation unit
 * (ref_input.c). This prevents GCC from seeing the string contents while
 * compiling this file, so parse_state() and rank_coordinates() cannot be
 * constant-folded from the known test vector.
 */
extern const char ref_input[];

int main(void)
{
    state_t state;

    if (!parse_state(ref_input, &state))
        return 2;

    uint16_t p, o;
    rank_coordinates(&state, &p, &o);

    uint8_t length = 0u;
    uint8_t h = heuristic(p, o);

    if (!solve(p, o, &length))
        return 1;

    /* Consume every solution byte so GCC must preserve the generated path. */
    uint32_t checksum = 0u;
    for (uint8_t i = 0u; i < length; ++i)
        checksum = checksum * 10u + (uint32_t)solution[i] + 1u;

    ref_p = p;
    ref_o = o;
    ref_solution_length = length;
    ref_root_heuristic = h;
    ref_search_nodes = search_nodes;
    ref_solution_checksum = checksum;

    return 0;
}
