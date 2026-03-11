@ Fault handler tests (UsageFault, HardFault escalation, CFSR/HFSR)
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
    .word _memfault + 1   @ 0x10: MemManage
    .word _busfault + 1   @ 0x14: BusFault
    .word _usagefault + 1 @ 0x18: UsageFault
    .word 0               @ 0x1C-0x28: Reserved
    .word 0
    .word 0
    .word 0
    .word _svc + 1        @ 0x2C: SVCall

    .text
    .thumb_func
    .global _start
_start:

    @ Test 1: CFSR readable and initially 0
    ldr r0, =0xE000ED28    @ CFSR
    ldr r1, [r0]
    cmp r1, #0
    bne fail1

    @ Test 2: HFSR readable and initially 0
    ldr r0, =0xE000ED2C    @ HFSR
    ldr r1, [r0]
    cmp r1, #0
    bne fail2

    @ Test 3: SHCSR read/write
    ldr r0, =0xE000ED24    @ SHCSR
    ldr r1, [r0]
    @ Enable UsageFault handler
    ldr r1, =(1 << 18)     @ USGFAULTENA
    str r1, [r0]
    ldr r2, [r0]
    ldr r3, =(1 << 18)
    tst r2, r3
    beq fail3

    @ Test 4: UDF triggers UsageFault when enabled
    movs r5, #0            @ r5 = UsageFault counter
    .short 0xDE00          @ UDF #0 — should trigger UsageFault
    @ After handler returns, r5 should be 1
    cmp r5, #1
    bne fail4

    @ Test 5: CFSR has UNDEFINSTR bit set after UDF
    ldr r0, =0xE000ED28    @ CFSR
    ldr r1, [r0]
    ldr r2, =(1 << 16)     @ UNDEFINSTR is bit 16 of CFSR (UFSR bit 0)
    tst r1, r2
    beq fail5
    @ Clear it (W1C)
    str r1, [r0]
    ldr r1, [r0]
    ldr r2, =(1 << 16)
    tst r1, r2
    bne fail5              @ should be cleared now

    @ Test 6: UDF escalates to HardFault when UsageFault disabled
    @ Disable UsageFault handler
    ldr r0, =0xE000ED24    @ SHCSR
    movs r1, #0
    str r1, [r0]
    movs r5, #0            @ r5 = counter
    movs r6, #0            @ r6 = HardFault flag
    .short 0xDE00          @ UDF — should escalate to HardFault
    @ After HardFault handler, r6 should be 1
    cmp r6, #1
    bne fail6

    @ Test 7: HFSR FORCED bit set after escalation
    ldr r0, =0xE000ED2C    @ HFSR
    ldr r1, [r0]
    ldr r2, =(1 << 30)     @ FORCED bit
    tst r1, r2
    beq fail7
    @ Clear it
    str r1, [r0]

    @ Test 8: MPU_TYPE readable (8 regions)
    ldr r0, =0xE000ED90    @ MPU_TYPE
    ldr r1, [r0]
    lsr r1, r1, #8         @ DREGION field bits [15:8]
    and r1, r1, #0xFF
    cmp r1, #8
    bne fail8

    @ Test 9: MPU_CTRL write/read
    ldr r0, =0xE000ED94    @ MPU_CTRL
    movs r1, #5            @ ENABLE + PRIVDEFENA
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, #5
    bne fail9
    @ Disable MPU
    movs r1, #0
    str r1, [r0]

    @ Test 10: MPU_RNR + MPU_RBAR write/read
    ldr r0, =0xE000ED98    @ MPU_RNR
    movs r1, #2            @ select region 2
    str r1, [r0]
    ldr r0, =0xE000ED9C    @ MPU_RBAR
    ldr r1, =0x20000000    @ base address
    str r1, [r0]
    ldr r2, [r0]
    ldr r3, =0x20000000
    cmp r2, r3
    bne fail10

    @ All tests passed
    movs r0, #0
    bkpt #0

    .thumb_func
_hardfault:
    @ Skip past the faulting UDF instruction (2 bytes)
    @ Get stacked PC from exception frame
    @ SP points to exception frame on MSP
    ldr r0, [sp, #24]     @ stacked PC (return address)
    adds r0, #2           @ skip UDF
    str r0, [sp, #24]     @ update stacked PC
    movs r6, #1           @ set HardFault flag
    bx lr

    .thumb_func
_memfault:
    bx lr

    .thumb_func
_busfault:
    bx lr

    .thumb_func
_usagefault:
    @ Skip past the faulting UDF instruction
    ldr r0, [sp, #24]     @ stacked PC
    adds r0, #2           @ skip UDF
    str r0, [sp, #24]
    adds r5, #1           @ increment UsageFault counter
    bx lr

    .thumb_func
_svc:
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

    .pool

    .section .stack, "w"
    .space 2048
_stack_top:
