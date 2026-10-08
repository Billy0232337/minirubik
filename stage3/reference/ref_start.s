    .section .text.start
    .globl _start

_start:
    li sp, 0x7ffffff0

    call main

    li a7, 10
    ecall
