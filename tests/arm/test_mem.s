@ Memory operations test (byte/half/word, LDM/STM, addressing modes)
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

    @ Test 1: STRB/LDRB
    ldr r0, =0x20000000
    movs r1, #0xAB
    strb r1, [r0]
    ldrb r2, [r0]
    cmp r2, #0xAB
    bne fail1

    @ Test 2: STRH/LDRH
    ldr r0, =0x20000010
    movw r1, #0x1234
    strh r1, [r0]
    ldrh r2, [r0]
    cmp r2, r1
    bne fail2

    @ Test 3: STR/LDR with register offset
    ldr r0, =0x20000020
    movs r1, #8
    movs r2, #0x42
    str r2, [r0, r1]
    ldr r3, [r0, r1]
    cmp r3, #0x42
    bne fail3

    @ Test 4: LDR with immediate offset
    ldr r0, =0x20000030
    movs r1, #11
    str r1, [r0, #4]
    ldr r2, [r0, #4]
    cmp r2, #11
    bne fail4

    @ Test 5: LDRSB (signed byte)
    ldr r0, =0x20000040
    movs r1, #0x80           @ negative byte
    strb r1, [r0]
    ldrsb r2, [r0, #0]
    ldr r3, =0xFFFFFF80
    cmp r2, r3
    bne fail5

    @ Test 6: LDRSH (signed halfword)
    ldr r0, =0x20000050
    movw r1, #0x8000          @ negative half
    strh r1, [r0]
    ldrsh r2, [r0, #0]
    ldr r3, =0xFFFF8000
    cmp r2, r3
    bne fail6

    @ Test 7: STM (store multiple, incrementing)
    ldr r0, =0x20000060
    movs r1, #11
    movs r2, #22
    movs r3, #33
    stm r0!, {r1, r2, r3}
    @ r0 should now point to 0x20000060 + 12 = 0x2000006C
    ldr r4, =0x2000006C
    cmp r0, r4
    bne fail7

    @ Test 8: LDM (load multiple, incrementing)
    ldr r0, =0x20000060
    ldm r0!, {r4, r5, r6}
    cmp r4, #11
    bne fail8
    cmp r5, #22
    bne fail8
    cmp r6, #33
    bne fail8

    @ Test 9: PUSH/POP with many registers
    movs r0, #1
    movs r1, #2
    movs r2, #3
    movs r3, #4
    push {r0-r3}
    movs r0, #0
    movs r1, #0
    movs r2, #0
    movs r3, #0
    pop {r0-r3}
    cmp r0, #1
    bne fail9
    cmp r3, #4
    bne fail9

    @ Test 10: PUSH/POP with LR/PC
    mov r4, lr              @ save LR
    movs r0, #77
    push {r0, lr}
    movs r0, #0
    pop {r0, r1}            @ pop into r0 and r1 (not PC)
    cmp r0, #77
    bne fail10
    mov lr, r4              @ restore LR

    @ Test 11: LDR PC-relative (literal pool)
    ldr r0, =0xCAFEBABE
    ldr r1, =0xCAFEBABE
    cmp r0, r1
    bne fail11

    @ Test 12: STR/LDR SP-relative
    movs r0, #55
    str r0, [sp, #0]
    movs r0, #0
    ldr r0, [sp, #0]
    cmp r0, #55
    bne fail12

    @ Test 13: LDRB with register offset
    ldr r0, =0x20000070
    ldr r1, =0x04030201
    str r1, [r0]
    movs r2, #2
    ldrb r3, [r0, r2]       @ byte at offset 2 = 0x03
    cmp r3, #3
    bne fail13

    @ Test 14: Multiple word store then verify
    ldr r0, =0x20000080
    movs r1, #0xAA
    movs r2, #0xBB
    movs r3, #0xCC
    str r1, [r0, #0]
    str r2, [r0, #4]
    str r3, [r0, #8]
    ldr r4, [r0, #0]
    ldr r5, [r0, #4]
    ldr r6, [r0, #8]
    cmp r4, #0xAA
    bne fail14
    cmp r5, #0xBB
    bne fail14
    cmp r6, #0xCC
    bne fail14

    @ Test 15: LDRH with immediate offset
    ldr r0, =0x20000090
    ldr r1, =0xBEEF1234
    str r1, [r0]
    ldrh r2, [r0, #0]       @ lower half = 0x1234
    movw r3, #0x1234
    cmp r2, r3
    bne fail15
    ldrh r2, [r0, #2]       @ upper half = 0xBEEF
    movw r3, #0xBEEF
    cmp r2, r3
    bne fail15

    @ Test 16: STRH with register offset
    ldr r0, =0x200000A0
    movs r1, #4
    movw r2, #0xABCD
    strh r2, [r0, r1]       @ store at offset 4
    ldrh r3, [r0, r1]
    cmp r3, r2
    bne fail16

    @ Test 17: LDRSB with register offset
    ldr r0, =0x200000B0
    movs r1, #0xFE          @ -2 as signed byte
    strb r1, [r0]
    movs r2, #0
    ldrsb r3, [r0, r2]
    ldr r4, =0xFFFFFFFE
    cmp r3, r4
    bne fail17

    @ Test 18: LDRSH with register offset
    ldr r0, =0x200000C0
    movw r1, #0xFFFE        @ -2 as signed halfword
    strh r1, [r0]
    movs r2, #0
    ldrsh r3, [r0, r2]
    ldr r4, =0xFFFFFFFE
    cmp r3, r4
    bne fail18

    @ Test 19: LDR.W pre-indexed with writeback
    ldr r0, =0x200000D0
    ldr r1, =0xDEADC0DE
    str r1, [r0, #8]        @ store at D0+8 = D8
    ldr.w r2, [r0, #8]!     @ pre-indexed: addr = r0+8, then r0 = r0+8
    cmp r2, r1
    bne fail19
    ldr r3, =0x200000D8
    cmp r0, r3               @ r0 should be updated
    bne fail19

    @ Test 20: STR.W/LDR.W with post-indexed
    ldr r0, =0x200000E0
    ldr r1, =0x12345678
    str r1, [r0]             @ store at E0
    ldr.w r2, [r0], #4       @ post-indexed: load from r0, then r0 += 4
    cmp r2, r1
    bne fail20
    ldr r3, =0x200000E4
    cmp r0, r3               @ r0 should be incremented
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
