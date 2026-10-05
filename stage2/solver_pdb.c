#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUBIES 7u
#define PERMUTATIONS 5040u
#define ORIENTATIONS 729u
#define STATES (PERMUTATIONS * ORIENTATIONS)
#define FACES 3u
#define MOVES 9u
#define MAX_DEPTH 11u
#define P3_STATES 210u
#define PDB_ROW_BYTES (P3_STATES / 2u)
#define PDB_BYTES (ORIENTATIONS * PDB_ROW_BYTES)

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
} state_t;

static const char *const move_names[MOVES] = {
    "R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"
};
static const uint8_t inverse_move[MOVES] = {2,1,0,5,4,3,8,7,6};

static const uint8_t source[FACES][CUBIES] = {
    {1,4,2,0,3,5,6},
    {0,1,2,4,5,6,3},
    {0,2,5,3,1,4,6},
};
static const uint8_t twist[FACES][CUBIES] = {
    {1,2,0,2,1,0,0},
    {0,0,0,1,2,1,2},
    {0,0,0,0,0,0,0},
};

static uint16_t permutation[FACES][PERMUTATIONS];
static uint16_t orientation[FACES][ORIENTATIONS];
static uint8_t perm_dist[PERMUTATIONS];
static uint8_t perm3_map[PERMUTATIONS];
static uint8_t pdb3_packed[PDB_BYTES];
static uint8_t solution[MAX_DEPTH];
static uint64_t search_nodes;

static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;
    for (unsigned i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        result.o[i] = (uint8_t)((state.o[from] + twist[face][i]) % 3u);
    }
    return result;
}

static state_t apply_move(state_t state, uint8_t move)
{
    uint8_t face = (uint8_t)(move / 3u);
    uint8_t turns = (uint8_t)(move % 3u + 1u);
    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, face);
    return state;
}

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

static void unrank_state(uint32_t rank, state_t *state)
{
    uint8_t available[CUBIES] = {0,1,2,3,4,5,6};
    uint32_t p = rank / ORIENTATIONS;
    uint32_t o = rank % ORIENTATIONS;
    uint32_t f = 720u;
    uint8_t sum = 0;

    for (unsigned i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t)(p / f);
        p %= f;
        state->p[i] = available[q];
        unsigned remaining = CUBIES - i;
        for (unsigned j = q; j + 1u < remaining; ++j)
            available[j] = available[j + 1u];
        if (i < 5u)
            f /= 6u - i;
    }

    for (unsigned i = 6u; i-- > 0u;) {
        state->o[i] = (uint8_t)(o % 3u);
        sum = (uint8_t)(sum + state->o[i]);
        o /= 3u;
    }
    state->o[6] = (uint8_t)((3u - sum % 3u) % 3u);
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

static void build_transitions(void)
{
    state_t state;
    for (uint16_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state((uint32_t)rank * ORIENTATIONS, &state);
        for (uint8_t face = 0; face < FACES; ++face) {
            state_t next = quarter_turn(state, face);
            permutation[face][rank] =
                (uint16_t)(rank_state(&next) / ORIENTATIONS);
        }
    }
    for (uint16_t rank = 0; rank < ORIENTATIONS; ++rank) {
        unrank_state(rank, &state);
        for (uint8_t face = 0; face < FACES; ++face) {
            state_t next = quarter_turn(state, face);
            orientation[face][rank] =
                (uint16_t)(rank_state(&next) % ORIENTATIONS);
        }
    }
}

static int build_perm_dist(void)
{
    uint16_t queue[PERMUTATIONS];
    uint16_t head = 0, tail = 1;
    memset(perm_dist, 0xff, sizeof perm_dist);
    queue[0] = 0;
    perm_dist[0] = 0;
    while (head < tail) {
        uint16_t here = queue[head++];
        for (uint8_t face = 0; face < FACES; ++face) {
            uint16_t next = here;
            for (uint8_t turn = 0; turn < 3u; ++turn) {
                next = permutation[face][next];
                if (perm_dist[next] == UINT8_MAX) {
                    perm_dist[next] = (uint8_t)(perm_dist[here] + 1u);
                    queue[tail++] = next;
                }
            }
        }
    }
    return tail == PERMUTATIONS;
}

static uint8_t rank_perm3_positions(uint8_t p0, uint8_t p1, uint8_t p2)
{
    unsigned r0 = p0;
    unsigned r1 = p1 - (p1 > p0 ? 1u : 0u);
    unsigned removed_before_p2 =
        (p0 < p2 ? 1u : 0u) + (p1 < p2 ? 1u : 0u);
    unsigned r2 = p2 - removed_before_p2;
    return (uint8_t)(r0 * 30u + r1 * 5u + r2);
}

static void unrank_perm3_positions(uint8_t rank,
                                   uint8_t *p0,
                                   uint8_t *p1,
                                   uint8_t *p2)
{
    uint8_t available[CUBIES] = {0,1,2,3,4,5,6};
    unsigned r0 = rank / 30u;
    unsigned rem = rank % 30u;
    unsigned r1 = rem / 5u;
    unsigned r2 = rem % 5u;

    *p0 = available[r0];
    for (unsigned j = r0; j + 1u < 7u; ++j)
        available[j] = available[j + 1u];
    *p1 = available[r1];
    for (unsigned j = r1; j + 1u < 6u; ++j)
        available[j] = available[j + 1u];
    *p2 = available[r2];
}

static void build_perm3_map(void)
{
    state_t state;
    for (uint16_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state((uint32_t)rank * ORIENTATIONS, &state);
        uint8_t pos[3] = {0,0,0};
        for (uint8_t position = 0; position < CUBIES; ++position) {
            uint8_t cubie = state.p[position];
            if (cubie < 3u)
                pos[cubie] = position;
        }
        perm3_map[rank] = rank_perm3_positions(pos[0], pos[1], pos[2]);
    }
}

static inline uint8_t pdb3_get(uint8_t p3, uint16_t o)
{
    size_t byte_index =
        (size_t)o * PDB_ROW_BYTES + (size_t)(p3 >> 1);
    uint8_t byte = pdb3_packed[byte_index];
    return (p3 & 1u) ? (uint8_t)(byte >> 4)
                     : (uint8_t)(byte & 0x0fu);
}

static inline void pdb3_set(uint8_t p3, uint16_t o, uint8_t value)
{
    size_t byte_index =
        (size_t)o * PDB_ROW_BYTES + (size_t)(p3 >> 1);
    uint8_t old = pdb3_packed[byte_index];
    if (p3 & 1u)
        pdb3_packed[byte_index] =
            (uint8_t)((old & 0x0fu) | (uint8_t)(value << 4));
    else
        pdb3_packed[byte_index] =
            (uint8_t)((old & 0xf0u) | value);
}

static int build_pdb3(void)
{
    uint8_t p3_move[FACES][P3_STATES];
    uint8_t new_position[FACES][CUBIES];

    for (uint8_t face = 0; face < FACES; ++face)
        for (uint8_t destination = 0; destination < CUBIES; ++destination)
            new_position[face][source[face][destination]] = destination;

    for (uint8_t face = 0; face < FACES; ++face) {
        for (unsigned p3 = 0; p3 < P3_STATES; ++p3) {
            uint8_t p0, p1, p2;
            unrank_perm3_positions((uint8_t)p3, &p0, &p1, &p2);
            p3_move[face][p3] = rank_perm3_positions(
                new_position[face][p0],
                new_position[face][p1],
                new_position[face][p2]
            );
        }
    }

    const size_t count = (size_t)P3_STATES * ORIENTATIONS;
    uint8_t *dist = malloc(count);
    uint32_t *queue = malloc(count * sizeof queue[0]);
    if (!dist || !queue) {
        free(dist);
        free(queue);
        return 0;
    }

    memset(dist, 0xff, count);
    uint32_t head = 0, tail = 1;
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

    if (tail != count) {
        free(dist);
        free(queue);
        return 0;
    }

    memset(pdb3_packed, 0xff, sizeof pdb3_packed);
    uint8_t max_value = 0;
    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        for (unsigned p3 = 0; p3 < P3_STATES; ++p3) {
            uint8_t d = dist[(size_t)o * P3_STATES + p3];
            if (d > 0x0fu) {
                free(dist);
                free(queue);
                return 0;
            }
            if (d > max_value)
                max_value = d;
            pdb3_set((uint8_t)p3, o, d);
        }
    }

    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        for (unsigned p3 = 0; p3 < P3_STATES; ++p3) {
            uint8_t ref = dist[(size_t)o * P3_STATES + p3];
            if (pdb3_get((uint8_t)p3, o) != ref) {
                free(dist);
                free(queue);
                return 0;
            }
        }
    }

    free(dist);
    free(queue);
    return max_value <= MAX_DEPTH && pdb3_get(0,0) == 0;
}

static inline uint8_t heuristic(uint16_t p, uint16_t o)
{
    uint8_t hp = perm_dist[p];
    uint8_t hpdb = pdb3_get(perm3_map[p], o);
    return hp > hpdb ? hp : hpdb;
}

static int dfs(uint16_t p, uint16_t o, uint8_t depth,
               uint8_t bound, int8_t previous_face)
{
    ++search_nodes;
    uint8_t h = heuristic(p, o);
    if ((unsigned)depth + h > bound)
        return 0;
    if (p == 0u && o == 0u)
        return 1;
    if (depth == bound)
        return 0;

    for (uint8_t face = 0; face < FACES; ++face) {
        if ((int8_t)face == previous_face)
            continue;
        uint16_t next_p = p;
        uint16_t next_o = o;
        for (uint8_t turn = 0; turn < 3u; ++turn) {
            next_p = permutation[face][next_p];
            next_o = orientation[face][next_o];
            solution[depth] = (uint8_t)(face * 3u + turn);
            if (dfs(next_p, next_o, (uint8_t)(depth + 1u),
                    bound, (int8_t)face))
                return 1;
        }
    }
    return 0;
}

static int solve(uint16_t p, uint16_t o, uint8_t *solution_length)
{
    uint8_t first_bound = heuristic(p,o);
    search_nodes = 0;
    for (uint8_t bound = first_bound; bound <= MAX_DEPTH; ++bound) {
        if (dfs(p,o,0,bound,-1)) {
            *solution_length = bound;
            return 1;
        }
    }
    return 0;
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
            state->o[i-7u] = (uint8_t)(input[i] - '1');
    }
    return valid(state);
}

static int self_test(void)
{
    const state_t solved = {{0,1,2,3,4,5,6},{0,0,0,0,0,0,0}};
    state_t state;

    for (uint8_t move = 0; move < MOVES; ++move) {
        state = solved;
        state = apply_move(state, move);
        state = apply_move(state, inverse_move[move]);
        if (memcmp(&state, &solved, sizeof solved) != 0) {
            fprintf(stderr, "move/inverse test failed at move %u\n", move);
            return 0;
        }
    }

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        unrank_state(rank, &state);
        if (!valid(&state) || rank_state(&state) != rank) {
            fprintf(stderr, "rank round-trip failed at rank %" PRIu32 "\n", rank);
            return 0;
        }
    }

    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        if (perm_dist[p] == UINT8_MAX || perm3_map[p] >= P3_STATES) {
            fprintf(stderr, "invalid permutation abstraction at %u\n", p);
            return 0;
        }
    }

    if (pdb3_get(0,0) != 0) {
        fputs("PDB solved entry is not zero\n", stderr);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    build_transitions();
    build_perm3_map();
    if (!build_perm_dist() || !build_pdb3()) {
        fputs("failed to build heuristic tables\n", stderr);
        return 1;
    }

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        if (!self_test()) {
            fputs("self-test failed\n", stderr);
            return 1;
        }
        puts("self-test passed");
        puts("5040 permutation states reached");
        puts("153090 pattern-database states reached");
        printf("static table bytes=%u\n",
               (unsigned)(sizeof permutation + sizeof orientation +
                          sizeof perm_dist + sizeof perm3_map +
                          sizeof pdb3_packed));
        return 0;
    }

    int show_stats = 0;
    const char *input = NULL;
    if (argc == 2) {
        input = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--stats") == 0) {
        show_stats = 1;
        input = argv[2];
    } else {
        fprintf(stderr, "usage: %s [--stats] PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver_pdb");
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

    if (!solve(p,o,&length)) {
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
        fprintf(stderr, "root_heuristic=%u\n", heuristic(p,o));
        fprintf(stderr, "search_nodes=%" PRIu64 "\n", search_nodes);
    }
    return 0;
}
