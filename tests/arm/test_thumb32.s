@ Thumb-32 instruction tests
@ R0 = 0 on success, R0 = test number on failure

    .syntax unified
    .thumb
    .cpu cortex-m4
    .fpu fpv4-sp-d16

    .section .vectors, "a"
    .word _stack_top
    .word _start + 1

    .text
    .thumb_func
    .global _start
_start:

    @ Test 1: MOVW (16-bit immediate)
    movw r0, #0xBEEF
    movw r1, #0xBEEF
    cmp r0, r1
    bne fail1

    @ Test 2: MOVT (load top halfword)
    movw r0, #0x1234
    movt r0, #0x5678
    ldr r1, =0x56781234
    cmp r0, r1
    bne fail2

    @ Test 3: SDIV
    movs r0, #42
    movs r1, #6
    sdiv r2, r0, r1
    cmp r2, #7
    bne fail3

    @ Test 4: UDIV
    movs r0, #100
    movs r1, #7
    udiv r2, r0, r1      @ 100/7 = 14
    cmp r2, #14
    bne fail4

    @ Test 5: SDIV negative
    movs r0, #42
    rsbs r0, r0, #0      @ r0 = -42
    movs r1, #6
    sdiv r2, r0, r1      @ -42/6 = -7
    adds r2, #7
    cmp r2, #0
    bne fail5

    @ Test 6: UMULL (unsigned 32x32→64)
    ldr r0, =0x10000
    ldr r1, =0x10000
    umull r2, r3, r0, r1  @ 0x10000 * 0x10000 = 0x100000000
    cmp r2, #0             @ low = 0
    bne fail6
    cmp r3, #1             @ high = 1
    bne fail6

    @ Test 7: SMULL (signed 32x32→64)
    movs r0, #100
    rsbs r0, r0, #0       @ r0 = -100
    movs r1, #100
    smull r2, r3, r0, r1  @ -100 * 100 = -10000
    ldr r4, =0xFFFFD8F0   @ -10000 low
    cmp r2, r4
    bne fail7
    ldr r4, =0xFFFFFFFF   @ -10000 high (sign extend)
    cmp r3, r4
    bne fail7

    @ Test 8: CLZ (count leading zeros)
    movs r0, #1
    clz r1, r0
    cmp r1, #31
    bne fail8

    @ Test 9: CLZ of 0
    movs r0, #0
    clz r1, r0
    cmp r1, #32
    bne fail9

    @ Test 10: RBIT (reverse bits)
    movs r0, #1           @ bit 0
    rbit r1, r0           @ → bit 31 = 0x80000000
    ldr r2, =0x80000000
    cmp r1, r2
    bne fail10

    @ Test 11: REV.W (byte reverse, 32-bit encoding)
    ldr r0, =0xAABBCCDD
    rev r1, r0
    ldr r2, =0xDDCCBBAA
    cmp r1, r2
    bne fail11

    @ Test 12: B.W (wide branch)
    b.w test12_target
    bkpt #0                @ should never reach
test12_back:

    @ Test 13: BL (branch with link)
    bl test13_func
    cmp r0, #99
    bne fail13

    @ Test 14: LDR.W with immediate offset
    ldr r0, =test_data
    ldr.w r1, [r0, #4]
    ldr r2, =0xDEADBEEF
    cmp r1, r2
    bne fail14

    @ Test 15: STR.W with immediate offset
    ldr r0, =0x20000100
    ldr r1, =0xCAFEBABE
    str.w r1, [r0, #8]
    ldr.w r2, [r0, #8]
    cmp r2, r1
    bne fail15

    @ Test 16: LDRD (load register double)
    ldr r0, =test_data
    ldrd r2, r3, [r0]
    ldr r4, =0x12345678
    cmp r2, r4
    bne fail16
    ldr r4, =0xDEADBEEF
    cmp r3, r4
    bne fail16

    @ Test 17: STRD (store register double)
    ldr r0, =0x20000200
    ldr r2, =0xAAAAAAAA
    ldr r3, =0xBBBBBBBB
    strd r2, r3, [r0]
    ldrd r4, r5, [r0]
    cmp r4, r2
    bne fail17
    cmp r5, r3
    bne fail17

    @ Test 18: MUL.W (32-bit encoding)
    movs r0, #12
    movs r1, #11
    mul r2, r0, r1
    cmp r2, #132
    bne fail18

    @ Test 19: ORR.W with shifted register
    movs r0, #0x01
    movs r1, #0x01
    orr.w r2, r0, r1, lsl #8   @ 0x01 | (0x01 << 8) = 0x0101
    movw r3, #0x0101
    cmp r2, r3
    bne fail19

    @ Test 20: BIC.W
    ldr r0, =0xFFFFFFFF
    movs r1, #0xFF
    bic.w r2, r0, r1     @ clear bottom 8 bits
    ldr r3, =0xFFFFFF00
    cmp r2, r3
    bne fail20

    @ Test 21: AND.W with immediate
    ldr r0, =0x12345678
    and.w r1, r0, #0xFF
    cmp r1, #0x78
    bne fail21

    @ Test 22: MRS (read PRIMASK)
    movs r0, #0
    mrs r0, PRIMASK
    cmp r0, #0             @ PRIMASK should be 0 (interrupts enabled)
    bne fail22

    @ Test 23: LDREX/STREX
    ldr r0, =0x20000300
    movs r1, #42
    str r1, [r0]
    ldrex r2, [r0]
    cmp r2, #42
    bne fail23
    movs r3, #99
    strex r4, r3, [r0]    @ r4 = 0 if success
    cmp r4, #0
    bne fail23
    ldr r5, [r0]
    cmp r5, #99
    bne fail23

    @ Test 24: UBFX (unsigned bit field extract)
    ldr r0, =0x12345678
    ubfx r1, r0, #8, #8   @ extract bits [15:8] = 0x56
    cmp r1, #0x56
    bne fail24

    @ Test 25: ADD.W with shifted register
    movs r0, #1
    movs r1, #1
    add.w r2, r0, r1, lsl #4  @ 1 + (1 << 4) = 17
    cmp r2, #17
    bne fail25

    @ All tests passed
    movs r0, #0
    bkpt #0

    .thumb_func
test12_target:
    b test12_back

    .thumb_func
test13_func:
    movs r0, #99
    bx lr

fail1:  movs r0, #1
    bkpt #0
fail2:  movs r0, #2
    bkpt #0
fail3:  movs r0, #3
    bkpt #0
fail4:  movs r0, #4
    bkpt #0
fail5:  movs r0, #5
    bkpt #0
fail6:  movs r0, #6
    bkpt #0
fail7:  movs r0, #7
    bkpt #0
fail8:  movs r0, #8
    bkpt #0
fail9:  movs r0, #9
    bkpt #0
fail10: movs r0, #10
    bkpt #0
fail11: movs r0, #11
    bkpt #0
fail12: movs r0, #12
    bkpt #0
fail13: movs r0, #13
    bkpt #0
fail14: movs r0, #14
    bkpt #0
fail15: movs r0, #15
    bkpt #0
fail16: movs r0, #16
    bkpt #0
fail17: movs r0, #17
    bkpt #0
fail18: movs r0, #18
    bkpt #0
fail19: movs r0, #19
    bkpt #0
fail20: movs r0, #20
    bkpt #0
fail21: movs r0, #21
    bkpt #0
fail22: movs r0, #22
    bkpt #0
fail23: movs r0, #23
    bkpt #0
fail24: movs r0, #24
    bkpt #0
fail25: movs r0, #25
    bkpt #0

    .align 2
test_data:
    .word 0x12345678
    .word 0xDEADBEEF

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
