.text
.globl main

main:
    lui  t0, 0x10000
    lui  t1, 0x10400
    li   t2, 0x55

loop:
    sw   t2, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, loop

    li   a7, 10
    ecall
