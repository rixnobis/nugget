/* Linux o32 entry for running bench code under qemu-mipsel user mode. */
    .set noreorder
    .section .text.start, "ax", @progbits
    .global __start
__start:
    la    $sp, qstack_top
    jal   qmain
    nop
    move  $a0, $v0
    li    $v0, 4001
    syscall
    .global qwrite
qwrite:
    move  $a2, $a1
    move  $a1, $a0
    li    $a0, 1
    li    $v0, 4004
    syscall
    jr    $ra
    nop
    .section .bss
    .balign 16
    .space 65536
qstack_top:
    .section .rodata
    .balign 4
    .global g_corpus
g_corpus:
    .incbin "../corpus.bin"
