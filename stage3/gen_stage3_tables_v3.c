#include <stdint.h>
#include <stdio.h>
#include "stage3_tables.h"

static void emit_u16_2d(const char *name, const uint16_t *data,
                        unsigned rows, unsigned cols)
{
    printf("static const uint16_t %s[%u][%u] = {\n", name, rows, cols);
    for (unsigned r = 0; r < rows; ++r) {
        puts("    {");
        for (unsigned c = 0; c < cols; ++c) {
            if ((c % 16u) == 0u) fputs("        ", stdout);
            printf("%u", data[r * cols + c]);
            if (c + 1u != cols) fputs(", ", stdout);
            if ((c % 16u) == 15u || c + 1u == cols) putchar('\n');
        }
        fputs(r + 1u == rows ? "    }\n" : "    },\n", stdout);
    }
    puts("};\n");
}

static void emit_u16_1d(const char *name, const uint16_t *data, unsigned n)
{
    printf("static const uint16_t %s[%u] = {\n", name, n);
    for (unsigned i = 0; i < n; ++i) {
        if ((i % 16u) == 0u) fputs("    ", stdout);
        printf("%u", data[i]);
        if (i + 1u != n) fputs(", ", stdout);
        if ((i % 16u) == 15u || i + 1u == n) putchar('\n');
    }
    puts("};\n");
}

static void emit_u32_1d(const char *name, const uint32_t *data, unsigned n)
{
    printf("static const uint32_t %s[%u] = {\n", name, n);
    for (unsigned i = 0; i < n; ++i) {
        if ((i % 12u) == 0u) fputs("    ", stdout);
        printf("%u", data[i]);
        if (i + 1u != n) fputs(", ", stdout);
        if ((i % 12u) == 11u || i + 1u == n) putchar('\n');
    }
    puts("};\n");
}

static void emit_u8_1d(const char *name, const uint8_t *data, unsigned n)
{
    printf("static const uint8_t %s[%u] = {\n", name, n);
    for (unsigned i = 0; i < n; ++i) {
        if ((i % 24u) == 0u) fputs("    ", stdout);
        printf("%u", data[i]);
        if (i + 1u != n) fputs(", ", stdout);
        if ((i % 24u) == 23u || i + 1u == n) putchar('\n');
    }
    puts("};\n");
}

int main(void)
{
    static uint16_t perm_meta[5040];
    static uint32_t pdb_row_offset[729];

    for (unsigned p = 0; p < 5040u; ++p)
        perm_meta[p] = (uint16_t)(((uint16_t)perm_dist[p] << 8) | perm3_map[p]);

    for (unsigned o = 0; o < 729u; ++o)
        pdb_row_offset[o] = o * 105u;

    puts("#ifndef STAGE3_TABLES_V3_H");
    puts("#define STAGE3_TABLES_V3_H");
    puts("#include <stdint.h>\n");
    emit_u16_2d("permutation", &permutation[0][0], 3, 5040);
    emit_u16_2d("orientation", &orientation[0][0], 3, 729);
    emit_u16_1d("perm_meta", perm_meta, 5040);
    emit_u32_1d("pdb_row_offset", pdb_row_offset, 729);
    emit_u8_1d("pdb3_packed", pdb3_packed, 76545);
    puts("#endif");
    return 0;
}
