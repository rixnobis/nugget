    .set push
    .set noreorder
    .set noat

/* 4 KB of straight-line code: running it replaces every i-cache line. */
    .section .text.benchSled, "ax", @progbits
    .balign 16
    .global benchSled
benchSled:
    .rept 1022
    nop
    .endr
    jr    $ra
    nop

/* void benchOnStack(void (*fn)(void), void *sp): runs fn on another stack,
   so the top of RAM is free for the edge-of-memory arm. The 16 bytes at
   0($sp) are fn's argument home area (a varargs callee stores a0-a3 there),
   so the saved registers sit above them. */
    .section .text.benchOnStack, "ax", @progbits
    .global benchOnStack
benchOnStack:
    move  $t0, $sp
    move  $sp, $a1
    addiu $sp, -24
    sw    $ra, 20($sp)
    sw    $t0, 16($sp)
    jalr  $a0
    nop
    lw    $ra, 20($sp)
    lw    $sp, 16($sp)
    jr    $ra
    nop

/* uint32_t benchTime(fn, src, end, dst, uint32_t *hblanks)
   Runs the sled, then calls fn(src, end, dst) between two reads of root
   counter 2 (system clock) and counter 1 (hblank). Returns the counter 2
   delta (mod 65536) and stores the counter 1 delta. */
    .section .text.benchTime, "ax", @progbits
    .global benchTime
benchTime:
    addiu $sp, -32
    sw    $ra, 28($sp)
    sw    $s0, 24($sp)
    sw    $s1, 20($sp)
    sw    $s2, 16($sp)
    sw    $s3, 12($sp)
    move  $s0, $a0
    move  $s1, $a1
    move  $s2, $a2
    move  $s3, $a3
    la    $t0, benchSled
    jalr  $t0
    nop
    move  $a0, $s1
    move  $a1, $s2
    move  $a2, $s3
    lui   $t0, 0x1f80
    lhu   $t1, 0x1120($t0)
    lhu   $t2, 0x1110($t0)
    sw    $t1, 8($sp)
    jalr  $s0
    sw    $t2, 4($sp)
    lui   $t0, 0x1f80
    lhu   $t1, 0x1120($t0)
    lhu   $t2, 0x1110($t0)
    lw    $t3, 8($sp)
    lw    $t4, 4($sp)
    subu  $v0, $t1, $t3
    andi  $v0, 0xffff
    subu  $t2, $t4
    andi  $t2, 0xffff
    lw    $t5, 48($sp)
    nop
    sw    $t2, 0($t5)
    lw    $ra, 28($sp)
    lw    $s0, 24($sp)
    lw    $s1, 20($sp)
    lw    $s2, 16($sp)
    lw    $s3, 12($sp)
    jr    $ra
    addiu $sp, 32
    .set pop
