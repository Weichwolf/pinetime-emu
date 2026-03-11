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
    cbz r0, fail8_t        @ should NOT branch (r0 != 0)
    movs r1, #1
    cmp r1, #1
    bne fail8
    b test9
fail8_t:
    b fail8
test9:

    @ Test 9: CBNZ — branch if nonzero
    movs r0, #42
    cbnz r0, test9_ok
    b fail9
test9_ok:

    @ Test 10: CBNZ — don't branch if zero
    movs r0, #0
    movs r1, #0
    cbnz r0, fail10_t      @ should NOT branch
    movs r1, #1
    cmp r1, #1
    bne fail10
    b test11
fail10_t:
    b fail10
test11:

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

    @ Test 16: IT CS (carry set) after ADDS
    ldr r0, =0xFFFFFFFF
    adds r0, #1            @ C=1, Z=1
    it cs
    movcs r1, #77
    cmp r1, #77
    bne fail16

    @ Test 17: IT CC (carry clear)
    movs r0, #5
    adds r0, #1            @ C=0
    it cc
    movcc r1, #88
    cmp r1, #88
    bne fail17

    @ Test 18: IT MI (negative)
    movs r0, #1
    subs r0, #5            @ N=1 (result is negative)
    it mi
    movmi r1, #33
    cmp r1, #33
    bne fail18

    @ Test 19: IT PL (positive or zero)
    movs r0, #0
    cmp r0, #0             @ Z=1, N=0
    it pl
    movpl r1, #44
    cmp r1, #44
    bne fail19

    @ Test 20: ITET (then-else-then)
    movs r0, #5
    movs r1, #0
    movs r2, #0
    movs r3, #0
    cmp r0, #5
    itet eq
    moveq r1, #1           @ then: execute
    movne r2, #2           @ else: skip
    moveq r3, #3           @ then: execute
    cmp r1, #1
    bne fail20
    cmp r2, #0             @ should NOT have been modified
    bne fail20
    cmp r3, #3
    bne fail20

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

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
