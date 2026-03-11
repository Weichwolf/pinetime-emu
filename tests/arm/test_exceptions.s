@ Exception handling tests (SVC, PendSV, SysTick, NVIC IRQ, MSR/MRS, nesting)
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
    .word 0               @ 0x1C: Reserved
    .word 0               @ 0x20: Reserved
    .word 0               @ 0x24: Reserved
    .word 0               @ 0x28: Reserved
    .word _svc + 1        @ 0x2C: SVCall
    .word 0               @ 0x30: Debug
    .word 0               @ 0x34: Reserved
    .word _pendsv + 1     @ 0x38: PendSV
    .word _systick + 1    @ 0x3C: SysTick
    .word _irq0 + 1       @ 0x40: IRQ0
    .word _irq1 + 1       @ 0x44: IRQ1

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

    @ Test 5: MRS read PRIMASK (should be 0)
    mrs r0, PRIMASK
    cmp r0, #0
    bne fail5

    @ Test 6: MSR/MRS PRIMASK round-trip
    movs r0, #1
    msr PRIMASK, r0
    mrs r1, PRIMASK
    cmp r1, #1
    bne fail6
    @ Restore PRIMASK=0
    movs r0, #0
    msr PRIMASK, r0

    @ Test 7: MSR/MRS BASEPRI
    movs r0, #0x40
    msr BASEPRI, r0
    mrs r1, BASEPRI
    cmp r1, #0x40
    bne fail7
    @ Restore BASEPRI=0
    movs r0, #0
    msr BASEPRI, r0

    @ Test 8: MSR/MRS FAULTMASK
    movs r0, #1
    msr FAULTMASK, r0
    mrs r1, FAULTMASK
    cmp r1, #1
    bne fail8
    @ Restore
    movs r0, #0
    msr FAULTMASK, r0

    @ Test 9: MSR/MRS CONTROL
    mrs r0, CONTROL
    cmp r0, #0          @ Default: thread mode, MSP, privileged
    bne fail9

    @ Test 10: MRS reads xPSR (T bit set)
    mrs r0, PSR
    ldr r1, =0x01000000  @ T bit at bit 24
    tst r0, r1
    beq fail10

    @ Test 11: PendSV trigger via ICSR
    ldr r5, =0           @ r5 = PendSV counter
    ldr r0, =0xE000ED04  @ ICSR
    ldr r1, =0x10000000  @ PENDSVSET
    str r1, [r0]
    @ PendSV should fire (it's lowest priority, but we're in thread mode)
    @ After return, r5 should be 1
    @ Need NOP for pipeline
    nop
    nop
    cmp r5, #1
    bne fail11

    @ Test 12: SysTick basic interrupt
    ldr r5, =0           @ r5 = SysTick counter
    @ Set SysTick RELOAD — must be big enough to span a tick_peripherals batch
    ldr r0, =0xE000E014  @ SYST_RVR
    movw r1, #5000
    str r1, [r0]
    @ Clear current value
    ldr r0, =0xE000E018  @ SYST_CVR
    movs r1, #0
    str r1, [r0]
    @ Enable SysTick with interrupt (CLKSOURCE=processor, TICKINT=1, ENABLE=1)
    ldr r0, =0xE000E010  @ SYST_CSR
    movs r1, #7          @ CLKSOURCE | TICKINT | ENABLE
    str r1, [r0]
    @ Execute enough NOPs to cross the tick_peripherals boundary (~10k+ cycles)
    movw r4, #15000
systick_loop:
    subs r4, #1
    bne systick_loop
    @ Disable SysTick
    ldr r0, =0xE000E010
    movs r1, #0
    str r1, [r0]
    @ r5 should be > 0 (SysTick fired at least once)
    cmp r5, #0
    beq fail12

    @ Test 13: NVIC IRQ0 trigger
    ldr r5, =0           @ r5 = IRQ0 counter
    @ Enable IRQ0 in NVIC
    ldr r0, =0xE000E100  @ NVIC_ISER0
    movs r1, #1          @ bit 0 = IRQ0
    str r1, [r0]
    @ Set IRQ0 pending
    ldr r0, =0xE000E200  @ NVIC_ISPR0
    movs r1, #1
    str r1, [r0]
    nop
    nop
    cmp r5, #1
    bne fail13
    @ Disable IRQ0
    ldr r0, =0xE000E180  @ NVIC_ICER0
    movs r1, #1
    str r1, [r0]

    @ Test 14: PRIMASK blocks IRQ
    movs r0, #1
    msr PRIMASK, r0       @ Mask all
    ldr r5, =0
    @ Enable + pend IRQ1
    ldr r0, =0xE000E100
    movs r1, #2          @ bit 1 = IRQ1
    str r1, [r0]
    ldr r0, =0xE000E200
    str r1, [r0]
    nop
    nop
    @ IRQ1 should NOT have fired (masked by PRIMASK)
    cmp r5, #0
    bne fail14
    @ Unmask and let it fire
    movs r0, #0
    msr PRIMASK, r0
    nop
    nop
    cmp r5, #1
    bne fail14
    @ Disable IRQ1
    ldr r0, =0xE000E180
    movs r1, #2
    str r1, [r0]

    @ Test 15: CPSID i / CPSIE i
    cpsid i               @ PRIMASK = 1
    mrs r0, PRIMASK
    cmp r0, #1
    bne fail15
    cpsie i               @ PRIMASK = 0
    mrs r0, PRIMASK
    cmp r0, #0
    bne fail15

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

    .thumb_func
_pendsv:
    adds r5, #1
    bx lr

    .thumb_func
_systick:
    adds r5, #1
    bx lr

    .thumb_func
_irq0:
    adds r5, #1
    bx lr

    .thumb_func
_irq1:
    adds r5, #1
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

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
