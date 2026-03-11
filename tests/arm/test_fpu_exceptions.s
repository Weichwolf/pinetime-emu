@ FPU exception context tests (lazy stacking, FPCA, S-register preservation)
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
    .word 0               @ 0x30: Debug
    .word 0               @ 0x34: Reserved
    .word 0               @ 0x38: PendSV
    .word 0               @ 0x3C: SysTick

    .text
    .thumb_func
    .global _start
_start:
    @ Enable FPU: set CP10/CP11 full access in CPACR
    ldr r0, =0xE000ED88
    ldr r1, [r0]
    orr r1, r1, #(0xF << 20)
    str r1, [r0]
    dsb
    isb

    @ Test 1: CONTROL.FPCA set after FPU use
    mrs r0, CONTROL
    tst r0, #4             @ FPCA should be 0 initially
    bne fail1
    vmov s0, r0            @ any FPU instruction sets FPCA
    mrs r0, CONTROL
    tst r0, #4             @ FPCA should be 1 now
    beq fail1

    @ Test 2: SVC preserves S0-S3 when FPU context is active
    @ Load known values into S0-S3
    ldr r0, =0x3F800000    @ 1.0f
    vmov s0, r0
    ldr r0, =0x40000000    @ 2.0f
    vmov s1, r0
    ldr r0, =0x40400000    @ 3.0f
    vmov s2, r0
    ldr r0, =0x40800000    @ 4.0f
    vmov s3, r0
    svc #0                 @ handler modifies S0
    @ S0 should be restored to 1.0f after return
    vmov r0, s0
    ldr r1, =0x3F800000
    cmp r0, r1
    bne fail2

    @ Test 3: S1-S3 also preserved across SVC
    vmov r0, s1
    ldr r1, =0x40000000    @ 2.0f
    cmp r0, r1
    bne fail3
    vmov r0, s2
    ldr r1, =0x40400000    @ 3.0f
    cmp r0, r1
    bne fail3
    vmov r0, s3
    ldr r1, =0x40800000    @ 4.0f
    cmp r0, r1
    bne fail3

    @ Test 4: Stack pointer correctly adjusted for FPU frame
    @ (SP should be back to original after SVC return)
    mov r4, sp
    svc #0
    mov r5, sp
    cmp r4, r5
    bne fail4

    @ Test 5: S registers 4-15 preserved across exception
    ldr r0, =0x41000000    @ 8.0f
    vmov s4, r0
    ldr r0, =0x41100000    @ 9.0f
    vmov s5, r0
    ldr r0, =0x41A00000    @ 20.0f
    vmov s15, r0
    svc #0
    vmov r0, s4
    ldr r1, =0x41000000
    cmp r0, r1
    bne fail5
    vmov r0, s15
    ldr r1, =0x41A00000
    cmp r0, r1
    bne fail5

    @ Test 6: FPSCR preserved across exception
    vmrs r0, FPSCR
    movs r4, r0             @ save original
    svc #0
    vmrs r0, FPSCR
    cmp r0, r4
    bne fail6

    @ Test 7: Multiple SVC calls with FPU context
    ldr r0, =0x42480000    @ 50.0f
    vmov s0, r0
    svc #0
    svc #0
    svc #0
    vmov r0, s0
    ldr r1, =0x42480000
    cmp r0, r1
    bne fail7

    @ All tests passed
    movs r0, #0
    bkpt #0

    .thumb_func
_hardfault:
    movs r0, #0xFF
    bkpt #0

    .thumb_func
_svc:
    @ Clobber S0 in handler to verify stacking works
    ldr r0, =0x4F000000    @ some different value
    vmov s0, r0
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

    .pool

    .section .stack, "w"
    .space 2048
_stack_top:
