# GCC RV32I reference build

This directory contains the freestanding GCC reference used for the Stage 4 comparison.
The solver algorithm is the final Stage 3 v3 algorithm; host-only stdio is removed.
The fixed input is kept in a separate translation unit so GCC cannot constant-fold it while compiling the solver, and the returned solution path is consumed through a checksum.

Build from `stage3/`:

```bash
riscv64-unknown-elf-gcc -O2 -march=rv32i -mabi=ilp32 -ffreestanding \
  -c reference/solver_reference.c -o reference/solver_reference.o

riscv64-unknown-elf-gcc -O2 -march=rv32i -mabi=ilp32 -ffreestanding \
  -c reference/ref_input.c -o reference/ref_input.o

riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles \
  reference/ref_start.s reference/solver_reference.o reference/ref_input.o \
  -Wl,-e,_start -o reference/solver_reference.elf
```

The measured fair-reference result was 1,807,350 retired instructions and 1,724 bytes of linked `.text` for the fixed distance-11 vector.
