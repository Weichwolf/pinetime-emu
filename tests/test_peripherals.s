@ nRF52 Peripheral register tests
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

    @ ---- Test 1: GPIO OUT write/read ----
    ldr r0, =0x50000504    @ GPIO OUT register
    movs r1, #0xFF
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, #0xFF
    bne fail1

    @ ---- Test 2: GPIO OUTSET ----
    ldr r0, =0x50000504    @ OUT
    movs r1, #0
    str r1, [r0]           @ clear OUT
    ldr r0, =0x50000508    @ OUTSET
    movs r1, #0x0F
    str r1, [r0]           @ set bits 0-3
    ldr r0, =0x50000504    @ read OUT
    ldr r2, [r0]
    cmp r2, #0x0F
    bne fail2

    @ ---- Test 3: GPIO OUTCLR ----
    ldr r0, =0x5000050C    @ OUTCLR
    movs r1, #0x03
    str r1, [r0]           @ clear bits 0-1
    ldr r0, =0x50000504    @ read OUT
    ldr r2, [r0]
    cmp r2, #0x0C          @ bits 2-3 remain
    bne fail3

    @ ---- Test 4: GPIO DIR ----
    ldr r0, =0x50000514    @ DIR
    movs r1, #0xFF
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, #0xFF
    bne fail4

    @ ---- Test 5: GPIO DIRSET ----
    ldr r0, =0x50000514    @ DIR
    movs r1, #0
    str r1, [r0]           @ clear DIR
    ldr r0, =0x50000518    @ DIRSET
    movs r1, #0x0F
    str r1, [r0]
    ldr r0, =0x50000514    @ read DIR
    ldr r2, [r0]
    cmp r2, #0x0F
    bne fail5

    @ ---- Test 6: GPIO PIN_CNF write/read ----
    ldr r0, =0x50000700    @ PIN_CNF[0]
    movs r1, #0x03         @ DIR=out, INPUT=disconnect
    str r1, [r0]
    ldr r2, [r0]
    @ Note: SENSE auto-toggle may modify, check DIR+INPUT bits only
    and.w r2, r2, #0x03
    cmp r2, #0x03
    bne fail6

    @ ---- Test 7: NVIC ISER/ICER ----
    ldr r0, =0xE000E100    @ NVIC_ISER0
    movs r1, #0x01         @ enable IRQ 0
    str r1, [r0]
    ldr r2, [r0]
    tst r2, r1
    beq fail7
    ldr r0, =0xE000E180    @ NVIC_ICER0
    str r1, [r0]           @ disable IRQ 0
    ldr r0, =0xE000E100    @ read ISER0
    ldr r2, [r0]
    tst r2, r1
    bne fail7              @ bit should be cleared

    @ ---- Test 8: SCB VTOR ----
    ldr r0, =0xE000ED08    @ VTOR
    ldr r1, [r0]
    @ VTOR should be 0 (our test vector table is at 0)
    cmp r1, #0
    bne fail8

    @ ---- Test 9: CLOCK HFCLKSTART task ----
    ldr r0, =0x40000000    @ CLOCK base
    movs r1, #1
    str r1, [r0]           @ TASKS_HFCLKSTART
    ldr r2, [r0, #0x100]   @ EVENTS_HFCLKSTARTED
    cmp r2, #1             @ should be instant
    bne fail9

    @ ---- Test 10: TIMER0 basic operation ----
    ldr r0, =0x40008000    @ TIMER0 base
    @ Set MODE = Timer
    movs r1, #0
    str r1, [r0, #0x504]   @ MODE = Timer
    @ Set PRESCALER = 0
    str r1, [r0, #0x510]   @ PRESCALER = 0
    @ Clear timer
    movs r1, #1
    str r1, [r0, #0x00C]   @ TASKS_CLEAR
    @ Start timer
    str r1, [r0, #0x000]   @ TASKS_START
    @ Capture (timer may be 0 or small since we just started)
    str r1, [r0, #0x040]   @ TASKS_CAPTURE[0]
    ldr r2, [r0, #0x540]   @ CC[0]
    @ Timer value should be 0 or very small
    cmp r2, #100
    bhi fail10             @ should be less than 100

    @ ---- Test 11: WDT register write ----
    ldr r0, =0x40010000    @ WDT base
    ldr r1, =0x00000001
    str r1, [r0, #0x600]   @ RR[0] = reload
    @ No crash = pass (WDT doesn't timeout in emulator)

    @ ---- Test 12: GPIOTE CONFIG write/read ----
    ldr r0, =0x40006510    @ GPIOTE CONFIG[0]
    ldr r1, =0x00010001    @ MODE=Event, PSEL=0, POLARITY=LoToHi
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, r1
    bne fail12

    @ ---- Test 13: SysTick CTRL ----
    ldr r0, =0xE000E010    @ SYST_CSR
    ldr r1, [r0]           @ Read current value
    @ SysTick should be accessible (no fault)
    @ Write RELOAD
    ldr r0, =0xE000E014    @ SYST_RVR
    ldr r1, =0x00FFFFFF    @ max reload
    str r1, [r0]
    ldr r2, [r0]
    ldr r3, =0x00FFFFFF
    cmp r2, r3
    bne fail13

    @ ---- Test 14: FICR DEVICEID ----
    ldr r0, =0x10000060    @ FICR DEVICEID[0]
    ldr r1, [r0]
    @ Should return some value (not zero = fault)
    @ Our emulator returns predefined IDs
    cmp r1, #0
    beq fail14

    @ ---- Test 15: RAM read/write at end of SRAM ----
    ldr r0, =0x2000FFF0    @ near end of 64KB SRAM
    ldr r1, =0xDEADC0DE
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, r1
    bne fail15

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

    .pool

    .section .stack, "w"
    .space 1024
_stack_top:
