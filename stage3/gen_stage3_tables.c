#define main solver_pdb_embedded_main
#include "solver_pdb.c"
#undef main

static void print_u16_2d(const char *name, const uint16_t *a, unsigned rows, unsigned cols) {
    printf("static const uint16_t %s[%u][%u] = {\n", name, rows, cols);
    for (unsigned r=0;r<rows;++r) {
        printf("  {\n    ");
        for (unsigned c=0;c<cols;++c) {
            printf("%u%s", a[r*cols+c], c+1==cols?"":",");
            if ((c+1)%24==0 && c+1<cols) printf("\n    ");
        }
        printf("\n  }%s\n", r+1==rows?"":",");
    }
    printf("};\n\n");
}

static void print_u8_1d(const char *name, const uint8_t *a, unsigned n) {
    printf("static const uint8_t %s[%u] = {\n  ", name, n);
    for (unsigned i=0;i<n;++i) {
        printf("%u%s", a[i], i+1==n?"":",");
        if ((i+1)%32==0 && i+1<n) printf("\n  ");
    }
    printf("\n};\n\n");
}

int main(void) {
    build_transitions();
    build_perm3_map();
    if (!build_perm_dist() || !build_pdb3()) return 1;
    puts("#ifndef STAGE3_TABLES_H");
    puts("#define STAGE3_TABLES_H");
    puts("#include <stdint.h>\n");
    print_u16_2d("permutation", &permutation[0][0], FACES, PERMUTATIONS);
    print_u16_2d("orientation", &orientation[0][0], FACES, ORIENTATIONS);
    print_u8_1d("perm_dist", perm_dist, PERMUTATIONS);
    print_u8_1d("perm3_map", perm3_map, PERMUTATIONS);
    print_u8_1d("pdb3_packed", pdb3_packed, PDB_BYTES);
    puts("#endif");
    return 0;
}
