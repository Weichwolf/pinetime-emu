@ IT blocks, CBZ/CBNZ tests
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

    @ Test 1: IT EQ — execute on equal
    movs r0, #5
    cmp r0, #5
    it eq
    moveq r1, #1
    cmp r1, #1
    bne fail1

    @ Test 2: IT NE — skip on equal
    movs r0, #5
    movs r1, #0
    cmp r0, #5
    it ne
    movne r1, #99          @ should NOT execute (Z=1)
    cmp r1, #0
    bne fail2

    @ Test 3: ITE (if-then-else)
    movs r0, #10
    cmp r0, #10
    ite eq
    moveq r1, #1           @ should execute
    movne r1, #2           @ should NOT execute
    cmp r1, #1
    bne fail3

    @ Test 4: ITE with false condition
    movs r0, #10
    cmp r0, #20
    ite eq
    moveq r1, #1           @ should NOT execute
    movne r1, #2           @ should execute
    cmp r1, #2
    bne fail4

    @ Test 5: ITTT (3 then)
    movs r0, #7
    cmp r0, #7
    ittt eq
    moveq r1, #10
    moveq r2, #20
    moveq r3, #30
    cmp r1, #10
    bne fail5
    cmp r2, #20
    bne fail5
    cmp r3, #30
    bne fail5

    @ Test 6: ITTE (2 then, 1 else)
    movs r0, #7
    cmp r0, #7
    itte eq
    moveq r1, #1
    moveq r2, #2
    movne r3, #3           @ else: should NOT execute
    cmp r1, #1
    bne fail6
    cmp r2, #2
    bne fail6

    @ Test 7: CBZ — branch if zero
    movs r0, #0
    cbz r0, test7_ok
    b fail7
test7_ok:

    @ Test 8: CBZ — don't branch if nonzero
    movs r0, #1
    movs r1, #0
    cbz r0, fail8_tramp    @ should NOT branch (r0 != 0)
    movs r1, #1
    cmp r1, #1
    bne fail8

    @ Test 9: CBNZ — branch if nonzero
    movs r0, #42
    cbnz r0, test9_ok
    b fail9
test9_ok:

    @ Test 10: CBNZ — don't branch if zero
    movs r0, #0
    movs r1, #0
    cbnz r0, fail10_tramp  @ should NOT branch
    movs r1, #1
    cmp r1, #1
    bne fail10

    @ Test 11: IT with CMP inside
    movs r0, #3
    cmp r0, #3
    ite eq
    cmpeq r0, #3           @ should execute, sets Z=1
    cmpne r0, #99          @ should not execute
    beq test11_ok           @ Z should be 1 from cmpeq
    b fail11
test11_ok:

    @ Test 12: IT GT
    movs r0, #10
    cmp r0, #5
    it gt
    movgt r1, #42
    cmp r1, #42
    bne fail12

    @ Test 13: IT LT
    movs r0, #3
    movs r1, #0
    cmp r0, #10
    it lt
    movlt r1, #55
    cmp r1, #55
    bne fail13

    @ Test 14: IT with ADD
    movs r0, #10
    movs r1, #5
    cmp r0, #10
    it eq
    addeq r1, #10
    cmp r1, #15
    bne fail14

    @ Test 15: Nested condition — IT after IT results
    movs r0, #1
    movs r1, #1
    cmp r0, r1
    it eq
    moveq r2, #100
    cmp r2, #100
    it eq
    moveq r3, #200
    cmp r3, #200
    bne fail15

    @ All tests passed
    movs r0, #0
    bkpt #0

fail8_tramp:
    b fail8
fail10_tramp:
    b fail10

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

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
