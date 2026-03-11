@ ALU operations test
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

    @ Test 1: AND register
    movs r0, #0xFF
    movs r1, #0x0F
    ands r0, r1
    cmp r0, #0x0F
    bne fail1

    @ Test 2: ORR register
    movs r0, #0xA0
    movs r1, #0x05
    orrs r0, r1
    cmp r0, #0xA5
    bne fail2

    @ Test 3: EOR register
    movs r0, #0xFF
    movs r1, #0x0F
    eors r0, r1
    cmp r0, #0xF0
    bne fail3

    @ Test 4: BIC register
    movs r0, #0xFF
    movs r1, #0x0F
    bics r0, r1
    cmp r0, #0xF0
    bne fail4

    @ Test 5: MVN (bitwise NOT)
    movs r0, #0
    mvns r0, r0
    adds r1, r0, #1     @ r1 = ~0 + 1 = 0
    cmp r1, #0
    bne fail5

    @ Test 6: NEG (RSB #0)
    movs r0, #42
    rsbs r0, r0, #0
    @ r0 should be -42 = 0xFFFFFFD6
    adds r0, #42
    cmp r0, #0
    bne fail6

    @ Test 7: TST (test bits, no writeback)
    movs r0, #0xFF
    movs r1, #0x10
    tst r0, r1
    beq fail7           @ bit should be set, Z=0

    @ Test 8: CMN (compare negative)
    movs r0, #5
    movs r1, #5
    rsbs r1, r1, #0     @ r1 = -5
    cmn r1, r0           @ -5 + 5 = 0, Z=1
    bne fail8

    @ Test 9: MUL
    movs r0, #7
    movs r1, #6
    muls r0, r1, r0
    cmp r0, #42
    bne fail9

    @ Test 10: LSR immediate
    movs r0, #64
    lsrs r0, r0, #3
    cmp r0, #8
    bne fail10

    @ Test 11: ASR immediate (sign extend)
    movw r0, #0
    movt r0, #0x8000     @ r0 = 0x80000000 (negative)
    asrs r0, r0, #31     @ arithmetic shift right 31 → 0xFFFFFFFF
    adds r0, #1          @ should be 0
    cmp r0, #0
    bne fail11

    @ Test 12: ROR register
    movs r0, #1
    movs r1, #4
    rors r0, r1           @ rotate right by 4: 0x00000001 → 0x10000000
    ldr r1, =0x10000000
    cmp r0, r1
    bne fail12

    @ Test 13: ADC (add with carry)
    movs r0, #0xFF
    movs r1, #1
    adds r0, r1           @ 0xFF + 1 = 0x100, C=0 (no carry in 32-bit)
    movs r0, #0xFF
    movs r1, #0
    adcs r0, r1           @ r0 + r1 + C(0) = 0xFF
    cmp r0, #0xFF
    bne fail13

    @ Test 14: SBC (subtract with borrow)
    @ First set carry by doing a large add
    ldr r0, =0xFFFFFFFF
    movs r1, #1
    adds r0, r1           @ sets C=1 (overflow in unsigned)
    movs r0, #10
    movs r1, #3
    sbcs r0, r1           @ r0 - r1 - !C = 10 - 3 - 0 = 7
    cmp r0, #7
    bne fail14

    @ Test 15: SXTB (sign extend byte)
    movs r0, #0x80       @ negative byte
    sxtb r0, r0
    ldr r1, =0xFFFFFF80
    cmp r0, r1
    bne fail15

    @ Test 16: UXTH (unsigned extend half)
    ldr r0, =0xFFFF1234
    uxth r0, r0
    movw r1, #0x1234
    cmp r0, r1
    bne fail16

    @ Test 17: SXTH (sign extend half)
    movw r0, #0x8000
    sxth r0, r0
    ldr r1, =0xFFFF8000
    cmp r0, r1
    bne fail17

    @ Test 18: REV (byte reverse word)
    ldr r0, =0x12345678
    rev r0, r0
    ldr r1, =0x78563412
    cmp r0, r1
    bne fail18

    @ Test 19: REV16 (byte reverse packed halfword)
    ldr r0, =0x12345678
    rev16 r0, r0
    ldr r1, =0x34127856
    cmp r0, r1
    bne fail19

    @ Test 20: UXTB (unsigned extend byte)
    ldr r0, =0xFFFFFF80
    uxtb r0, r0
    cmp r0, #0x80
    bne fail20

    @ Test 21: REVSH (signed byte-reverse halfword)
    ldr r0, =0x000000FF   @ lower half = 0x00FF, reversed = 0xFF00, sign-extended
    revsh r0, r0
    ldr r1, =0xFFFFFF00
    cmp r0, r1
    bne fail21

    @ Test 22: SUB sets N flag (negative result)
    movs r0, #5
    subs r0, #10          @ 5 - 10 = -5 → N=1
    bpl fail22            @ should be negative

    @ Test 23: ADD sets C flag (carry out)
    ldr r0, =0xFFFFFFFF
    adds r0, #1           @ overflow → C=1, Z=1
    bcc fail23            @ carry should be set

    @ Test 24: ADD sets V flag (signed overflow)
    ldr r0, =0x7FFFFFFF   @ MAX_INT
    adds r0, #1           @ overflow → V=1
    bvc fail24            @ overflow should be set

    @ Test 25: LSL sets C from shifted-out bit
    movw r0, #0
    movt r0, #0x8000     @ r0 = 0x80000000
    lsls r0, r0, #1     @ shift left, MSB goes to C
    bcc fail25           @ carry should be set

    @ All tests passed
    movs r0, #0
    bkpt #0

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

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
