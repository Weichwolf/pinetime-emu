@ VFPv4 Fused Multiply-Add/Subtract tests
@ VFMA, VFMS, VFNMA, VFNMS — single and double precision
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
    @ VFMA.F32: Sd = Sd + Sn * Sm  (fused)
    @ ============================================================

    @ Test 1: VFMA.F32 basic: 1.0 + 2.0*3.0 = 7.0
    vmov.f32 s0, #1.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vfma.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0x40E00000       @ 7.0
    cmp r0, r1
    bne fail1

    @ Test 2: VFMA.F32 accumulate twice: 0 + 2*3 = 6, then 6 + 2*3 = 12
    vmov.f32 s0, #2.0
    vmov.f32 s1, #3.0
    vsub.f32 s4, s4, s4       @ s4 = 0
    vfma.f32 s4, s0, s1       @ s4 = 0 + 6 = 6
    vfma.f32 s4, s0, s1       @ s4 = 6 + 6 = 12
    vmov r0, s4
    ldr r1, =0x41400000       @ 12.0
    cmp r0, r1
    bne fail2

    @ Test 3: VFMA.F32 with negative operand: 10 + (-3)*4 = -2
    vmov.f32 s0, #10.0
    vmov.f32 s1, #3.0
    vneg.f32 s1, s1           @ s1 = -3.0
    vmov.f32 s2, #4.0
    vfma.f32 s0, s1, s2       @ s0 = 10 + (-3)*4 = -2
    vmov r0, s0
    ldr r1, =0xC0000000       @ -2.0
    cmp r0, r1
    bne fail3

    @ ============================================================
    @ VFMS.F32: Sd = Sd - Sn * Sm  (actually Sd + (-Sn)*Sm, fused)
    @ ============================================================

    @ Test 4: VFMS.F32 basic: 10.0 - 2.0*3.0 = 4.0
    vmov.f32 s0, #10.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vfms.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0x40800000       @ 4.0
    cmp r0, r1
    bne fail4

    @ Test 5: VFMS.F32 negative result: 1.0 - 3.0*2.0 = -5.0
    vmov.f32 s0, #1.0
    vmov.f32 s1, #3.0
    vmov.f32 s2, #2.0
    vfms.f32 s0, s1, s2       @ s0 = 1 - 6 = -5
    vmov r0, s0
    ldr r1, =0xC0A00000       @ -5.0
    cmp r0, r1
    bne fail5

    @ ============================================================
    @ VFNMA.F32: Sd = -Sd - Sn * Sm = -(Sd + Sn*Sm)  (fused)
    @ ============================================================

    @ Test 6: VFNMA.F32: -(1.0 + 2.0*3.0) = -7.0
    vmov.f32 s0, #1.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vfnma.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0xC0E00000       @ -7.0
    cmp r0, r1
    bne fail6

    @ Test 7: VFNMA.F32 with zero accumulator: -(0 + 4*5) = -20
    vsub.f32 s0, s0, s0      @ s0 = 0
    vmov.f32 s1, #4.0
    vmov.f32 s2, #5.0
    vfnma.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0xC1A00000       @ -20.0
    cmp r0, r1
    bne fail7

    @ ============================================================
    @ VFNMS.F32: Sd = -Sd + Sn * Sm = -(Sd - Sn*Sm)  (fused)
    @ ============================================================

    @ Test 8: VFNMS.F32: -(10.0 - 2.0*3.0) = -(10-6) = -4.0
    vmov.f32 s0, #10.0
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vfnms.f32 s0, s1, s2
    vmov r0, s0
    ldr r1, =0xC0800000       @ -4.0
    cmp r0, r1
    bne fail8

    @ Test 9: VFNMS.F32: -(1.0 - 3.0*2.0) = -(1-6) = 5.0
    vmov.f32 s0, #1.0
    vmov.f32 s1, #3.0
    vmov.f32 s2, #2.0
    vfnms.f32 s0, s1, s2     @ -(1 - 6) = 5
    vmov r0, s0
    ldr r1, =0x40A00000       @ 5.0
    cmp r0, r1
    bne fail9

    @ ============================================================
    @ VFMA/VFMS with higher S registers
    @ ============================================================

    @ Test 10: VFMA.F32 with S16..S31 range
    vmov.f32 s0, #2.0
    vmov s16, r0              @ s16 gets raw bits, fix:
    ldr r0, =0x40000000       @ 2.0
    vmov s16, r0
    ldr r0, =0x40400000       @ 3.0
    vmov s17, r0
    ldr r0, =0x3F800000       @ 1.0
    vmov s18, r0
    vfma.f32 s18, s16, s17    @ s18 = 1.0 + 2.0*3.0 = 7.0
    vmov r0, s18
    ldr r1, =0x40E00000       @ 7.0
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

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
