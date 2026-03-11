@ VFP (FPU) operations test
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
    @ Enable FPU: set CP10/CP11 full access in CPACR
    ldr r0, =0xE000ED88
    ldr r1, [r0]
    orr r1, r1, #(0xF << 20)
    str r1, [r0]
    dsb
    isb

    @ Test 1: VMOV immediate float
    vmov.f32 s0, #1.0
    vmov r0, s0
    ldr r1, =0x3F800000       @ IEEE 754 for 1.0
    cmp r0, r1
    bne fail1

    @ Test 2: VADD.F32
    vmov.f32 s0, #1.0
    vmov.f32 s1, #2.0
    vadd.f32 s2, s0, s1
    vmov r0, s2
    ldr r1, =0x40400000       @ 3.0
    cmp r0, r1
    bne fail2

    @ Test 3: VSUB.F32
    vmov.f32 s0, #5.0
    vmov.f32 s1, #2.0
    vsub.f32 s2, s0, s1
    vmov r0, s2
    ldr r1, =0x40400000       @ 3.0
    cmp r0, r1
    bne fail3

    @ Test 4: VMUL.F32
    vmov.f32 s0, #3.0
    vmov.f32 s1, #4.0
    vmul.f32 s2, s0, s1
    vmov r0, s2
    ldr r1, =0x41400000       @ 12.0
    cmp r0, r1
    bne fail4

    @ Test 5: VDIV.F32
    vmov.f32 s0, #10.0
    vmov.f32 s1, #2.0
    vdiv.f32 s2, s0, s1
    vmov r0, s2
    ldr r1, =0x40A00000       @ 5.0
    cmp r0, r1
    bne fail5

    @ Test 6: VNEG.F32
    vmov.f32 s0, #3.0
    vneg.f32 s1, s0
    vmov r0, s1
    ldr r1, =0xC0400000       @ -3.0
    cmp r0, r1
    bne fail6

    @ Test 7: VABS.F32
    vmov.f32 s0, #3.0
    vneg.f32 s0, s0           @ s0 = -3.0
    vabs.f32 s1, s0
    vmov r0, s1
    ldr r1, =0x40400000       @ 3.0
    cmp r0, r1
    bne fail7

    @ Test 8: VCMP + VMRS (compare and transfer flags)
    vmov.f32 s0, #3.0
    vmov.f32 s1, #2.0
    vcmp.f32 s0, s1
    vmrs APSR_nzcv, FPSCR     @ transfer FP flags to APSR
    ble fail8                  @ 3.0 > 2.0, should not be LE

    @ Test 9: VCMP equal
    vmov.f32 s0, #5.0
    vmov.f32 s1, #5.0
    vcmp.f32 s0, s1
    vmrs APSR_nzcv, FPSCR
    bne fail9

    @ Test 10: VCVT float→int
    vmov.f32 s0, #7.0
    vcvt.s32.f32 s1, s0
    vmov r0, s1
    cmp r0, #7
    bne fail10

    @ Test 11: VCVT int→float
    movs r0, #42
    vmov s0, r0
    vcvt.f32.s32 s1, s0
    vmov r1, s1
    ldr r2, =0x42280000       @ 42.0
    cmp r1, r2
    bne fail11

    @ Test 12: VLDR/VSTR
    ldr r0, =0x20000100
    vmov.f32 s0, #8.0
    vstr s0, [r0]
    vsub.f32 s1, s1, s1    @ s1 = 0.0
    vldr s1, [r0]
    vcmp.f32 s0, s1
    vmrs APSR_nzcv, FPSCR
    bne fail12

    @ Test 13: VMOV between two ARM regs and double
    ldr r0, =0x12345678
    ldr r1, =0x9ABCDEF0
    vmov d0, r0, r1
    vmov r2, r3, d0
    cmp r2, r0
    bne fail13
    cmp r3, r1
    bne fail13

    @ Test 14: VSQRT.F32
    vmov.f32 s0, #4.0
    vsqrt.f32 s1, s0
    vmov r0, s1
    ldr r1, =0x40000000       @ 2.0
    cmp r0, r1
    bne fail14

    @ Test 15: VMOV.F32 register to register
    vmov.f32 s0, #7.0
    vmov.f32 s4, s0
    vcmp.f32 s4, s0
    vmrs APSR_nzcv, FPSCR
    bne fail15

    @ Test 16: VCVT unsigned int→float
    ldr r0, =200
    vmov s0, r0
    vcvt.f32.u32 s1, s0
    vmov r1, s1
    ldr r2, =0x43480000       @ 200.0
    cmp r1, r2
    bne fail16

    @ Test 17: VCVT float→unsigned int
    vmov.f32 s0, #10.0
    vcvt.u32.f32 s1, s0
    vmov r0, s1
    cmp r0, #10
    bne fail17

    @ Test 18: VMLA.F32 (multiply-accumulate: Sd += Sn * Sm)
    vmov.f32 s0, #2.0         @ accumulator
    vmov.f32 s1, #3.0
    vmov.f32 s2, #4.0
    vmla.f32 s0, s1, s2       @ s0 = 2.0 + 3.0*4.0 = 14.0
    vmov r0, s0
    ldr r1, =0x41600000       @ 14.0
    cmp r0, r1
    bne fail18

    @ Test 19: VNMUL.F32 (negated multiply)
    vmov.f32 s0, #3.0
    vmov.f32 s1, #5.0
    vnmul.f32 s2, s0, s1      @ s2 = -(3.0*5.0) = -15.0
    vmov r0, s2
    ldr r1, =0xC1700000       @ -15.0
    cmp r0, r1
    bne fail19

    @ Test 20: VCMP less-than
    vmov.f32 s0, #1.0
    vmov.f32 s1, #2.0
    vcmp.f32 s0, s1
    vmrs APSR_nzcv, FPSCR
    bge fail20                 @ 1.0 < 2.0, should be LT

    @ Test 21: VLDM/VSTM (load/store multiple FP regs)
    ldr r0, =0x20000200
    vmov.f32 s8, #1.0
    vmov.f32 s9, #2.0
    vmov.f32 s10, #3.0
    vstm r0, {s8-s10}
    @ Clear and reload
    vsub.f32 s8, s8, s8
    vsub.f32 s9, s9, s9
    vsub.f32 s10, s10, s10
    vldm r0, {s8-s10}
    @ Verify
    vmov r1, s8
    ldr r2, =0x3F800000       @ 1.0
    cmp r1, r2
    bne fail21
    vmov r1, s10
    ldr r2, =0x40400000       @ 3.0
    cmp r1, r2
    bne fail21

    @ Test 22: VMOV from two core regs to consecutive S regs
    movs r0, #0
    movs r1, #0
    ldr r0, =0x3F800000       @ 1.0
    ldr r1, =0x40000000       @ 2.0
    vmov s20, r0
    vmov s21, r1
    vmov r2, s20
    vmov r3, s21
    cmp r2, r0
    bne fail22
    cmp r3, r1
    bne fail22

    @ Test 23: VCMP against zero
    vsub.f32 s0, s0, s0   @ s0 = 0.0
    vcmp.f32 s0, #0.0
    vmrs APSR_nzcv, FPSCR
    bne fail23

    @ Test 24: VMLS.F32 (multiply-subtract: Sd -= Sn * Sm)
    vmov.f32 s0, #10.0        @ accumulator
    vmov.f32 s1, #2.0
    vmov.f32 s2, #3.0
    vmls.f32 s0, s1, s2       @ s0 = 10.0 - 2.0*3.0 = 4.0
    vmov r0, s0
    ldr r1, =0x40800000       @ 4.0
    cmp r0, r1
    bne fail24

    @ Test 25: VSTM/VLDM with writeback
    ldr r0, =0x20000300
    vmov.f32 s12, #5.0
    vmov.f32 s13, #6.0
    vstmia r0!, {s12, s13}    @ store 2 floats, advance r0 by 8
    ldr r1, =0x20000308
    cmp r0, r1
    bne fail25
    @ Load back from base
    ldr r0, =0x20000300
    vsub.f32 s14, s14, s14
    vsub.f32 s15, s15, s15
    vldmia r0!, {s14, s15}
    vmov r1, s14
    ldr r2, =0x40A00000       @ 5.0
    cmp r1, r2
    bne fail25
    vmov r1, s15
    ldr r2, =0x40C00000       @ 6.0
    cmp r1, r2
    bne fail25

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
fail21: movs r0, #21
    bkpt #0
fail22: movs r0, #22
    bkpt #0
fail23: movs r0, #23
    bkpt #0
fail24: movs r0, #24
    bkpt #0
fail25: movs r0, #25
    bkpt #0

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
