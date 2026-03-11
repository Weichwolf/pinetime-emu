@ Basic CPU test for pinetime-emu
@ Tests: MOV, ADD, SUB, CMP, B, BL, LDR, STR, PUSH, POP
@ Result: R0 = 0 on success, R0 = test number on failure
@ Terminates via BKPT

    .syntax unified
    .thumb
    .cpu cortex-m4

    .section .vectors, "a"
    .word _stack_top      @ Initial SP
    .word _start + 1      @ Reset vector (thumb bit set)

    .text
    .thumb_func
    .global _start
_start:
    @ Test 1: MOV immediate
    movs r0, #42
    cmp r0, #42
    bne fail1

    @ Test 2: ADD immediate
    movs r1, #10
    adds r1, #20
    cmp r1, #30
    bne fail2

    @ Test 3: SUB immediate
    movs r2, #50
    subs r2, #15
    cmp r2, #35
    bne fail3

    @ Test 4: ADD register
    movs r3, #7
    movs r4, #8
    adds r5, r3, r4
    cmp r5, #15
    bne fail4

    @ Test 5: LDR/STR
    ldr r0, =0x20000000
    movs r1, #0xAA
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, #0xAA
    bne fail5

    @ Test 6: PUSH/POP
    movs r0, #11
    movs r1, #22
    push {r0, r1}
    movs r0, #0
    movs r1, #0
    pop {r0, r1}
    cmp r0, #11
    bne fail6
    cmp r1, #22
    bne fail6

    @ Test 7: BL (function call)
    bl add_func
    cmp r0, #30
    bne fail7

    @ Test 8: Logical ops
    movs r0, #0xFF
    movs r1, #0x0F
    ands r0, r1
    cmp r0, #0x0F
    bne fail8

    @ Test 9: Shift
    movs r0, #1
    lsls r0, r0, #4
    cmp r0, #16
    bne fail9

    @ Test 10: Wide MOV (MOVW)
    movw r0, #0x1234
    movt r0, #0x5678
    ldr r1, =0x56781234
    cmp r0, r1
    bne fail10

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

    .thumb_func
add_func:
    movs r0, #10
    movs r1, #20
    adds r0, r0, r1
    bx lr

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
