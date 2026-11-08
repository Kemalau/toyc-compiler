.text

.p2align 2
.globl main
.type main, @function
main:
    li t6, 32
    sub sp, sp, t6
    add t6, sp, t6
    sd ra, -8(t6)
    sd s0, -16(t6)
    mv s0, t6
    li t0, 5
    sw t0, -24(s0)
    li a0, 5
    j .Ltoyc_0_return
.Ltoyc_0_return:
    ld ra, -8(s0)
    mv sp, s0
    ld s0, -16(sp)
    ret
.size main, .-main

.section .note.GNU-stack,"",@progbits
