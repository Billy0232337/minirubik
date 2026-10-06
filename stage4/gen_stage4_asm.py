#!/usr/bin/env python3
"""Generate Ripes-friendly RV32I assembly tables and a heuristic smoke test.

Usage:
    python3 gen_stage4_asm.py stage3_tables_v3.h [output_dir]

Outputs in output_dir (default: current directory):
    stage4_tables.s
    stage4_heuristic_smoke.s
"""
from __future__ import annotations
import re
import sys
from pathlib import Path


def extract_array(text: str, name: str) -> list[int]:
    m = re.search(rf"\b{name}\s*\[[^;=]+?\]\s*=\s*\{{", text)
    if not m:
        raise SystemExit(f"cannot find array {name}")
    start = m.end() - 1
    depth = 0
    end = None
    for i in range(start, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end is None:
        raise SystemExit(f"unterminated array {name}")
    return [int(x) for x in re.findall(r"\b\d+\b", text[start:end + 1])]


def emit_values(f, directive: str, values: list[int], per_line: int) -> None:
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        f.write(f"    {directive} " + ", ".join(map(str, chunk)) + "\n")


def heuristic(p: int, o: int, perm_meta: list[int], row: list[int], pdb: list[int]) -> int:
    meta = perm_meta[p]
    p3 = meta & 0xff
    hp = (meta >> 8) & 0xff
    idx = row[o] + (p3 >> 1)
    packed = pdb[idx]
    hpdb = (packed >> ((p3 & 1) << 2)) & 0x0f
    return max(hp, hpdb)


def write_full_tables(path: Path, permutation, orientation, perm_meta, row, pdb) -> None:
    with path.open("w") as f:
        f.write("# Auto-generated from stage3_tables_v3.h.\n")
        f.write("# Static data only; intended to be concatenated with hand-written RV32I code.\n")
        f.write(".data\n")
        f.write(".align 2\n")
        f.write("permutation:\n")
        emit_values(f, ".half", permutation, 12)
        f.write("\n.align 2\norientation:\n")
        emit_values(f, ".half", orientation, 12)
        f.write("\n.align 2\nperm_meta:\n")
        emit_values(f, ".half", perm_meta, 12)
        f.write("\n.align 2\npdb_row_offset:\n")
        emit_values(f, ".word", row, 8)
        f.write("\n.align 2\npdb3_packed:\n")
        emit_values(f, ".byte", pdb, 24)


def write_smoke(path: Path, perm_meta, row, pdb) -> None:
    tests = [(0, 0), (720, 0), (5, 0), (100, 100)]
    expected = [heuristic(p, o, perm_meta, row, pdb) for p, o in tests]
    with path.open("w") as f:
        f.write("# Stage 4a heuristic smoke test for Ripes / RV32I.\n")
        f.write("# Expected: prints 'PASS 4' and exits normally.\n")
        f.write("# Tests include solved, fixed distance-11 permutation, known worst permutation,\n")
        f.write("# and an odd-p3 nibble case.\n\n")
        f.write(".data\n")
        f.write('msg_pass: .string "PASS "\n')
        f.write('msg_fail: .string "FAIL test "\n')
        f.write('newline:  .string "\\n"\n\n')
        f.write(".align 2\nperm_meta:\n")
        emit_values(f, ".half", perm_meta, 12)
        f.write("\n.align 2\npdb_row_offset:\n")
        emit_values(f, ".word", row, 8)
        f.write("\n.align 2\npdb3_packed:\n")
        emit_values(f, ".byte", pdb, 24)

        f.write("\n.text\n.globl main\nmain:\n")
        f.write("    li s0, 0              # number of passed tests\n")
        f.write("    li s1, 1              # 1-based test number\n\n")
        for idx, ((p, o), h) in enumerate(zip(tests, expected), 1):
            f.write(f"    # test {idx}: h(p={p}, o={o}) == {h}\n")
            f.write(f"    li a0, {p}\n    li a1, {o}\n    jal ra, heuristic\n")
            f.write(f"    li t0, {h}\n    bne a0, t0, fail\n")
            f.write("    addi s0, s0, 1\n")
            if idx != len(tests):
                f.write("    addi s1, s1, 1\n")
            f.write("\n")
        f.write("    la a0, msg_pass\n    li a7, 4\n    ecall\n")
        f.write("    mv a0, s0\n    li a7, 1\n    ecall\n")
        f.write("    la a0, newline\n    li a7, 4\n    ecall\n")
        f.write("    li a7, 10\n    ecall\n\n")
        f.write("fail:\n")
        f.write("    la a0, msg_fail\n    li a7, 4\n    ecall\n")
        f.write("    mv a0, s1\n    li a7, 1\n    ecall\n")
        f.write("    la a0, newline\n    li a7, 4\n    ecall\n")
        f.write("    li a7, 10\n    ecall\n\n")

        f.write("# heuristic(a0=p, a1=o) -> a0=max(h_perm, h_pdb)\n")
        f.write("# Uses only RV32I instructions (la/li/mv/ret are assembler pseudos).\n")
        f.write("heuristic:\n")
        f.write("    slli t0, a0, 1        # byte offset = p * 2\n")
        f.write("    la   t1, perm_meta\n")
        f.write("    add  t0, t1, t0\n")
        f.write("    lhu  t2, 0(t0)        # t2 = [hp:8 | p3:8]\n")
        f.write("    andi t3, t2, 255      # p3\n")
        f.write("    srli t4, t2, 8        # hp\n\n")
        f.write("    slli t0, a1, 2        # o * sizeof(uint32_t)\n")
        f.write("    la   t1, pdb_row_offset\n")
        f.write("    add  t0, t1, t0\n")
        f.write("    lw   t5, 0(t0)        # packed-PDB row byte offset\n")
        f.write("    srli t6, t3, 1        # p3 / 2\n")
        f.write("    add  t5, t5, t6\n")
        f.write("    la   t1, pdb3_packed\n")
        f.write("    add  t5, t1, t5\n")
        f.write("    lbu  t5, 0(t5)        # byte holding two 4-bit distances\n\n")
        f.write("    andi t6, t3, 1        # even/odd p3\n")
        f.write("    slli t6, t6, 2        # shift = 0 or 4\n")
        f.write("    srl  t5, t5, t6\n")
        f.write("    andi t5, t5, 15       # hpdb\n\n")
        f.write("    bgeu t4, t5, heuristic_hp\n")
        f.write("    mv   a0, t5\n")
        f.write("    ret\n")
        f.write("heuristic_hp:\n")
        f.write("    mv   a0, t4\n")
        f.write("    ret\n")


def main() -> None:
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: gen_stage4_asm.py stage3_tables_v3.h [output_dir]")
    src = Path(sys.argv[1])
    text = src.read_text()
    permutation = extract_array(text, "permutation")
    orientation = extract_array(text, "orientation")
    perm_meta = extract_array(text, "perm_meta")
    row = extract_array(text, "pdb_row_offset")
    pdb = extract_array(text, "pdb3_packed")

    expected_lengths = {
        "permutation": 3 * 5040,
        "orientation": 3 * 729,
        "perm_meta": 5040,
        "pdb_row_offset": 729,
        "pdb3_packed": 76545,
    }
    actual = {
        "permutation": len(permutation),
        "orientation": len(orientation),
        "perm_meta": len(perm_meta),
        "pdb_row_offset": len(row),
        "pdb3_packed": len(pdb),
    }
    for name, n in expected_lengths.items():
        if actual[name] != n:
            raise SystemExit(f"{name}: expected {n} values, got {actual[name]}")

    out_dir = Path(sys.argv[2]) if len(sys.argv) == 3 else Path.cwd()
    out_dir.mkdir(parents=True, exist_ok=True)
    write_full_tables(out_dir / "stage4_tables.s", permutation, orientation, perm_meta, row, pdb)
    write_smoke(out_dir / "stage4_heuristic_smoke.s", perm_meta, row, pdb)
    print("generated:")
    print(out_dir / "stage4_tables.s")
    print(out_dir / "stage4_heuristic_smoke.s")
    print("smoke expected heuristics:")
    for p, o in [(0, 0), (720, 0), (5, 0), (100, 100)]:
        print(f"  h({p}, {o}) = {heuristic(p, o, perm_meta, row, pdb)}")

if __name__ == "__main__":
    main()
