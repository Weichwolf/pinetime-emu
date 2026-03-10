@ Exception handling test (SVC entry/return)
@ R0 = 0 on success, R0 = test number on failure

    .syntax unified
    .thumb
    .cpu cortex-m4
    .fpu fpv4-sp-d16

    .section .vectors, "a"
    .word _stack_top      @ 0x00: SP
    .word _start + 1      @ 0x04: Reset
    .word 0               @ 0x08: NMI
    .word _hardfault + 1  @ 0x0C: HardFault
    .word 0               @ 0x10: MemManage
    .word 0               @ 0x14: BusFault
    .word 0               @ 0x18: UsageFault
    .word 0               @ 0x1C-0x28: Reserved
    .word 0
    .word 0
    .word 0
    .word _svc + 1        @ 0x2C: SVCall

    .text
    .thumb_func
    .global _start
_start:

    @ Test 1: SVC invocation and return
    movs r7, #0
    svc #0
    @ After return, r7 should be 42 (set by handler)
    cmp r7, #42
    bne fail1

    @ Test 2: SVC preserves stacked registers
    movs r0, #11
    movs r1, #22
    movs r2, #33
    movs r3, #44
    svc #0
    cmp r0, #11
    bne fail2
    cmp r1, #22
    bne fail2
    cmp r2, #33
    bne fail2
    cmp r3, #44
    bne fail2

    @ Test 3: Multiple SVC calls
    movs r6, #0
    svc #0
    adds r6, #1
    svc #0
    adds r6, #1
    svc #0
    adds r6, #1
    cmp r6, #3
    bne fail3

    @ Test 4: Stack pointer restored after SVC
    mov r4, sp
    svc #0
    mov r5, sp
    cmp r4, r5
    bne fail4

    @ All tests passed
    movs r0, #0
    bkpt #0

    .thumb_func
_hardfault:
    movs r0, #0xFF
    bkpt #0

    .thumb_func
_svc:
    movs r7, #42
    bx lr

fail1:  movs r0, #1
    bkpt #0
fail2:  movs r0, #2
    bkpt #0
fail3:  movs r0, #3
    bkpt #0
fail4:  movs r0, #4
    bkpt #0

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
