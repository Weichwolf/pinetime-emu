@ VFP Negated Multiply-Accumulate tests
@ VNMLA, VNMLS, VNMUL — single precision
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
    @ Enable FPU
    ldr r0, =0xE000ED88
    ldr r1, [r0]
    orr r1, r1, #(0xF << 20)
    str r1, [r0]
    dsb
    isb

    @ ============================================================
    @ VNMLA.F32: Sd = -Sd - Sn*Sm = -(Sd + Sn*Sm)
    @ ============================================================

    @ Test 1: VNMLA.F32: -(1.0 + 2.0*3.0) = -(1+6) = -7.0
    vmov.f32 s0, #1.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vnmla.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0xC0E00000       @ -7.0
    cmp r0, r1
    bne fail1

    @ Test 2: VNMLA.F32 with zero accum: -(0 + 4*5) = -20
    vsub.f32 s0, s0, s0       @ s0 = 0
    vmov.f32 s1, #4.0
    vmov.f32 s2, #5.0
    vnmla.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0xC1A00000       @ -20.0
    cmp r0, r1
    bne fail2

    @ Test 3: VNMLA.F32 with negative accum: -(-10 + 2*3) = -(−4) = 4.0
    vmov.f32 s0, #10.0
    vneg.f32 s0, s0            @ s0 = -10.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vnmla.f32 s0, s1, s2      @ -(-10 + 6) = -(−4) = 4
    vmov r0, s0
    ldr r1, =0x40800000       @ 4.0
    cmp r0, r1
    bne fail3

    @ ============================================================
    @ VNMLS.F32: Sd = -Sd + Sn*Sm = -(Sd - Sn*Sm)
    @ ============================================================

    @ Test 4: VNMLS.F32: -(10.0 - 2.0*3.0) = -(10-6) = -4.0
    vmov.f32 s0, #10.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vnmls.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0xC0800000       @ -4.0
    cmp r0, r1
    bne fail4

    @ Test 5: VNMLS.F32: -(1.0 - 3.0*2.0) = -(1-6) = 5.0
    vmov.f32 s0, #1.0
    vmov.f32 s1, #3.0
    vmov.f32 s2, #2.0
    vnmls.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0x40A00000       @ 5.0
    cmp r0, r1
    bne fail5

    @ Test 6: VNMLS.F32 product larger than accum: -(2 - 5*4) = -(−18) = 18
    vmov.f32 s0, #2.0
    vmov.f32 s1, #5.0
    vmov.f32 s2, #4.0
    vnmls.f32 s0, s1, s2      @ -(2 - 20) = 18
    vmov r0, s0
    ldr r1, =0x41900000       @ 18.0
    cmp r0, r1
    bne fail6

    @ ============================================================
    @ VNMUL.F32: Sd = -(Sn * Sm)
    @ ============================================================

    @ Test 7: VNMUL.F32 basic: -(3.0*5.0) = -15.0
    vmov.f32 s0, #3.0
    vmov.f32 s1, #5.0
    vnmul.f32 s2, s0, s1
    vmov r0, s2
    ldr r1, =0xC1700000       @ -15.0
    cmp r0, r1
    bne fail7

    @ Test 8: VNMUL.F32 with negative: -((-2)*4) = 8
    vmov.f32 s0, #2.0
    vneg.f32 s0, s0            @ s0 = -2.0
    vmov.f32 s1, #4.0
    vnmul.f32 s2, s0, s1      @ -((-2)*4) = 8
    vmov r0, s2
    ldr r1, =0x41000000       @ 8.0
    cmp r0, r1
    bne fail8

    @ Test 9: VNMUL.F32 with 1.0: -(1*7) = -7
    vmov.f32 s0, #1.0
    vmov.f32 s1, #7.0
    vnmul.f32 s2, s0, s1
    vmov r0, s2
    ldr r1, =0xC0E00000       @ -7.0
    cmp r0, r1
    bne fail9

    @ ============================================================
    @ Cross-check: VNMLA vs VFNMA (same semantics, different encoding)
    @ ============================================================

    @ Test 10: VNMLA and VFNMA should give same result
    vmov.f32 s0, #2.0
    vmov.f32 s4, #2.0
    vmov.f32 s1, #3.0
    vmov.f32 s2, #4.0
    vnmla.f32 s0, s1, s2      @ -(2 + 3*4) = -14
    vfnma.f32 s4, s1, s2      @ -(2 + 3*4) = -14
    vmov r0, s0
    vmov r1, s4
    cmp r0, r1
    bne fail10

    @ Test 11: VNMLS and VFNMS should give same result
    vmov.f32 s0, #5.0
    vmov.f32 s4, #5.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vnmls.f32 s0, s1, s2      @ -(5 - 2*3) = -(5-6) = 1
    vfnms.f32 s4, s1, s2      @ -(5 - 2*3) = 1
    vmov r0, s0
    vmov r1, s4
    cmp r0, r1
    bne fail11

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

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
