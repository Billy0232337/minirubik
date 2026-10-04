.text
.globl main

main:
    lui  t0, 0x10000
    li   t2, 0x55

    sw   t2, 0(t0)

    li   a7, 10
    ecall
