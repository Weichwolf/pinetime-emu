#include "test.hpp"
#include "bus.hpp"
#include "cpu.hpp"
#include "device/nvic.hpp"
#include "device/memory.hpp"

using namespace pt;

void test_reset() {
    RAM ram(64 * 1024);
    Flash flash(512 * 1024);
    // Vector table: SP at 0x00, PC at 0x04
    uint8_t vt[8] = {0x00, 0x00, 0x01, 0x20, // SP = 0x20010000
                      0x09, 0x00, 0x00, 0x00}; // Reset = 0x09 (thumb)
    memcpy(flash.data(), vt, sizeof(vt));
    // Put a BKPT at 0x08
    flash.data()[8] = 0x00; flash.data()[9] = 0xBE; // BKPT #0

    Bus bus;
    bus.set_fast_path(flash.data(), flash.size(), ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bus.map(0xE000E000, 0x1000, &nvic);

    CPU cpu(&bus);
    cpu.set_nvic(&nvic);
    nvic.set_irq_pending_flag(&cpu.irq_pending);
    nvic.set_event_reg(&cpu.event_reg);
    cpu.reset();

    ASSERT_EQ(cpu.r[13], 0x20010000u);
    ASSERT_EQ(cpu.r[15], 0x08u);
    ASSERT(cpu.xpsr & (1u << 24)); // T bit set
}

void test_flags() {
    Bus bus;
    CPU cpu(&bus);

    cpu.set_flags(true, false, true, false);
    ASSERT(cpu.flag_n());
    ASSERT(!cpu.flag_z());
    ASSERT(cpu.flag_c());
    ASSERT(!cpu.flag_v());

    cpu.set_nz(0);
    ASSERT(!cpu.flag_n());
    ASSERT(cpu.flag_z());

    cpu.set_nz(0x80000000);
    ASSERT(cpu.flag_n());
    ASSERT(!cpu.flag_z());
}

void test_it_state() {
    Bus bus;
    CPU cpu(&bus);
    cpu.xpsr = 0;
    ASSERT(!cpu.in_it_block());
}

void test_sp_banking() {
    Bus bus;
    CPU cpu(&bus);
    cpu.xpsr = 0; // thread mode
    cpu.control = 0;
    cpu.msp = 0x20001000;
    cpu.r[13] = 0x20001000;
    ASSERT_EQ(cpu.sp(), 0x20001000u);

    cpu.control = 2; // SPSEL=1
    cpu.psp = 0x20002000;
    cpu.r[13] = 0x20002000;
    ASSERT_EQ(cpu.sp(), 0x20002000u);

    // In handler mode, always MSP
    cpu.xpsr = 11; // exception active
    ASSERT_EQ(cpu.sp(), 0x20001000u);
}

void test_step_bkpt() {
    Flash flash(1024);
    RAM ram(1024);
    // BKPT #0 at address 0
    flash.data()[0] = 0x00; flash.data()[1] = 0xBE;

    Bus bus;
    bus.set_fast_path(flash.data(), flash.size(), ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bus.map(0xE000E000, 0x1000, &nvic);

    CPU cpu(&bus);
    cpu.set_nvic(&nvic);
    cpu.xpsr = (1u << 24); // T bit
    cpu.r[15] = 0;
    cpu.step();
    ASSERT(cpu.halted);
}

// Helper: build a minimal firmware image with vector table and code
struct MiniEnv {
    Flash flash{512 * 1024};
    RAM ram{64 * 1024};
    Bus bus;
    NVIC nvic;
    CPU cpu{&bus};

    MiniEnv() {
        bus.set_fast_path(flash.data(), flash.size(), ram.data(), 0x20000000, ram.size());
        bus.map(0xE000E000, 0x1000, &nvic);
        cpu.set_nvic(&nvic);
        nvic.set_irq_pending_flag(&cpu.irq_pending);
        nvic.set_event_reg(&cpu.event_reg);
    }

    void set_vector(int idx, uint32_t addr) {
        uint32_t off = idx * 4;
        flash.data()[off] = addr; flash.data()[off+1] = addr >> 8;
        flash.data()[off+2] = addr >> 16; flash.data()[off+3] = addr >> 24;
    }

    void write_thumb16(uint32_t addr, uint16_t inst) {
        flash.data()[addr] = inst & 0xFF;
        flash.data()[addr+1] = inst >> 8;
    }

    void write_thumb32(uint32_t addr, uint16_t hw1, uint16_t hw2) {
        write_thumb16(addr, hw1);
        write_thumb16(addr + 2, hw2);
    }
};

void test_exception_entry_return() {
    MiniEnv e;
    // Vector table: SP=0x20010000, Reset=0x100+1
    e.set_vector(0, 0x20010000); // Initial SP
    e.set_vector(1, 0x101);      // Reset vector (thumb)
    // SVC handler at 0x200
    e.set_vector(11, 0x201);     // SVCall vector

    // Code at 0x100: SVC #0 (0xDF00), then BKPT (0xBE00)
    e.write_thumb16(0x100, 0xDF00); // SVC #0
    e.write_thumb16(0x102, 0xBE00); // BKPT (return here after SVC)

    // SVC handler at 0x200: BX LR (0x4770) — return from exception
    e.write_thumb16(0x200, 0x4770);

    e.cpu.reset();
    ASSERT_EQ(e.cpu.r[15], 0x100u);
    ASSERT_EQ(e.cpu.r[13], 0x20010000u);

    // Execute SVC
    e.cpu.step();
    // SVC should trigger exception, now in handler at 0x200
    e.cpu.check_exceptions();
    ASSERT_EQ(e.cpu.r[15], 0x200u);
    ASSERT(e.cpu.in_handler_mode());
    // LR should be EXC_RETURN (0xFFFFFFF9 = Thread/MSP)
    ASSERT_EQ(e.cpu.r[14], 0xFFFFFFF9u);

    // Execute BX LR (exception return)
    e.cpu.step();
    // Should return to 0x102 (instruction after SVC)
    ASSERT_EQ(e.cpu.r[15], 0x102u);
    ASSERT(!e.cpu.in_handler_mode());
}

void test_exception_context_preservation() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);
    e.set_vector(11, 0x201); // SVCall

    // Code at 0x100: set R0=0x42, then SVC, then BKPT
    e.write_thumb16(0x100, 0x2042); // MOV R0, #0x42
    e.write_thumb16(0x102, 0xDF00); // SVC
    e.write_thumb16(0x104, 0xBE00); // BKPT

    // SVC handler: MOV R0, #0xFF, then BX LR
    e.write_thumb16(0x200, 0x20FF); // MOV R0, #0xFF
    e.write_thumb16(0x202, 0x4770); // BX LR

    e.cpu.reset();
    e.cpu.step(); // MOV R0, #0x42
    ASSERT_EQ(e.cpu.r[0], 0x42u);

    e.cpu.step(); // SVC
    e.cpu.check_exceptions(); // Enter handler
    ASSERT_EQ(e.cpu.r[15], 0x200u);

    e.cpu.step(); // MOV R0, #0xFF (in handler)
    ASSERT_EQ(e.cpu.r[0], 0xFFu);

    e.cpu.step(); // BX LR (exception return)
    // R0 should be restored to 0x42
    ASSERT_EQ(e.cpu.r[0], 0x42u);
    ASSERT_EQ(e.cpu.r[15], 0x104u);
}

void test_wfi_halts_cpu() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // WFI = 0xBF30
    e.write_thumb16(0x100, 0xBF30); // WFI
    e.write_thumb16(0x102, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.step(); // WFI
    ASSERT(e.cpu.wfi);
}

void test_wfi_wakeup_on_pending_irq() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    e.write_thumb16(0x100, 0xBF30); // WFI
    e.write_thumb16(0x102, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.step(); // WFI -> cpu.wfi = true
    ASSERT(e.cpu.wfi);

    // Pend IRQ 5 (enabled)
    e.nvic.write(0x100, Width::Word, 1u << 5); // ISER
    e.nvic.set_pending(5);
    e.cpu.check_exceptions(); // Should wake from WFI
    ASSERT(!e.cpu.wfi);
}

void test_run_batch() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // Simple loop: MOV R0, #0; ADD R0, #1; ADD R0, #1; BKPT
    e.write_thumb16(0x100, 0x2000); // MOV R0, #0
    e.write_thumb16(0x102, 0x3001); // ADD R0, #1
    e.write_thumb16(0x104, 0x3001); // ADD R0, #1
    e.write_thumb16(0x106, 0x3001); // ADD R0, #1
    e.write_thumb16(0x108, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100); // Run up to 100 cycles
    ASSERT(e.cpu.halted); // Should hit BKPT
    ASSERT_EQ(e.cpu.r[0], 3u);
}

void test_irq_preemption() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);
    // IRQ 5 handler at 0x300 (exception 21 = IRQ 5 + 16)
    e.set_vector(21, 0x301);

    // Code: NOP, NOP, BKPT
    e.write_thumb16(0x100, 0xBF00); // NOP
    e.write_thumb16(0x102, 0xBF00); // NOP
    e.write_thumb16(0x104, 0xBE00); // BKPT

    // IRQ handler: BX LR
    e.write_thumb16(0x300, 0x4770);

    e.cpu.reset();
    // Enable IRQ 5
    e.nvic.write(0x100, Width::Word, 1u << 5);

    e.cpu.step(); // NOP at 0x100
    ASSERT_EQ(e.cpu.r[15], 0x102u);

    // Pend IRQ 5
    e.nvic.set_pending(5);
    e.cpu.check_exceptions();
    // Should enter IRQ handler
    ASSERT_EQ(e.cpu.r[15], 0x300u);
    ASSERT(e.cpu.in_handler_mode());
    ASSERT_EQ(e.cpu.active_exception(), 21u); // IRQ5 = exception 21

    // Execute BX LR
    e.cpu.step();
    // Back to thread mode at 0x102
    ASSERT_EQ(e.cpu.r[15], 0x102u);
    ASSERT(!e.cpu.in_handler_mode());
}

void test_primask_blocks_irq() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);
    e.set_vector(21, 0x301);

    e.write_thumb16(0x100, 0xBF00); // NOP
    e.write_thumb16(0x102, 0xBE00); // BKPT
    e.write_thumb16(0x300, 0x4770); // BX LR

    e.cpu.reset();
    e.cpu.primask = 1; // Block all interrupts
    e.nvic.write(0x100, Width::Word, 1u << 5);
    e.nvic.set_pending(5);

    e.cpu.step(); // NOP
    e.cpu.check_exceptions();
    // Should NOT enter handler (PRIMASK blocks)
    ASSERT(!e.cpu.in_handler_mode());
    ASSERT_EQ(e.cpu.r[15], 0x102u);
}

void test_basepri_masks_lower_priority() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);
    e.set_vector(21, 0x301); // IRQ 5

    e.write_thumb16(0x100, 0xBF00);
    e.write_thumb16(0x102, 0xBE00);
    e.write_thumb16(0x300, 0x4770);

    e.cpu.reset();
    // Set BASEPRI=0x40, IRQ 5 priority=0x80 (lower = blocked)
    e.cpu.basepri = 0x40;
    e.nvic.write(0x100, Width::Word, 1u << 5);
    e.nvic.write(0x404, Width::Word, 0x00008000); // IPR[5] = 0x80
    e.nvic.set_pending(5);

    e.cpu.step();
    e.cpu.check_exceptions();
    // 0x80 >= 0x40, should be blocked
    ASSERT(!e.cpu.in_handler_mode());

    // Now set IRQ priority higher (0x20 < 0x40)
    e.nvic.write(0x404, Width::Word, 0x00002000); // IPR[5] = 0x20
    e.cpu.check_exceptions();
    ASSERT(e.cpu.in_handler_mode());
}

// ============================================================
// Thumb-16 instruction execution tests
// ============================================================

void test_thumb16_mov_add_sub() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #10; MOV R1, #20; ADD R2, R0, R1; SUB R3, R1, R0; BKPT
    e.write_thumb16(0x100, 0x200A); // MOV R0, #10
    e.write_thumb16(0x102, 0x2114); // MOV R1, #20
    e.write_thumb16(0x104, 0x1842); // ADD R2, R0, R1 (Rm=1,Rn=0,Rd=2)
    e.write_thumb16(0x106, 0x1A0B); // SUB R3, R1, R0
    e.write_thumb16(0x108, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 10u);
    ASSERT_EQ(e.cpu.r[1], 20u);
    ASSERT_EQ(e.cpu.r[2], 30u);
    ASSERT_EQ(e.cpu.r[3], 10u);
}

void test_thumb16_cmp_flags() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #5; CMP R0, #5; BKPT
    e.write_thumb16(0x100, 0x2005); // MOV R0, #5
    e.write_thumb16(0x102, 0x2805); // CMP R0, #5
    e.write_thumb16(0x104, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT(e.cpu.flag_z()); // 5 == 5
    ASSERT(e.cpu.flag_c()); // No borrow
    ASSERT(!e.cpu.flag_n());
}

void test_thumb16_ldr_str() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00); // SP inside SRAM (not at boundary)
    e.set_vector(1, 0x101);

    // Use SP-relative: STR R0, [SP, #0]; LDR R2, [SP, #0]
    e.write_thumb16(0x100, 0x2042); // MOV R0, #0x42
    e.write_thumb16(0x102, 0x9000); // STR R0, [SP, #0]
    e.write_thumb16(0x104, 0x9A00); // LDR R2, [SP, #0]
    e.write_thumb16(0x106, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[2], 0x42u);
}

void test_thumb16_push_pop() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // MOV R0, #0xAA; PUSH {R0}; MOV R0, #0; POP {R0}; BKPT
    e.write_thumb16(0x100, 0x20AA); // MOV R0, #0xAA
    e.write_thumb16(0x102, 0xB401); // PUSH {R0}
    e.write_thumb16(0x104, 0x2000); // MOV R0, #0
    e.write_thumb16(0x106, 0xBC01); // POP {R0}
    e.write_thumb16(0x108, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 0xAAu);
}

void test_thumb16_shifts() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #1; LSL R0, R0, #4 → R0=16; LSR R1, R0, #2 → R1=4; BKPT
    // LSL Rd, Rm, #imm5: 0000 0|imm5|Rm|Rd
    // LSL R0, R0, #4: 0000 0|00100|000|000 = 0x0100
    // LSR R1, R0, #2: 0000 1|00010|000|001 = 0x0881
    e.write_thumb16(0x100, 0x2001); // MOV R0, #1
    e.write_thumb16(0x102, 0x0100); // LSL R0, R0, #4
    e.write_thumb16(0x104, 0x0881); // LSR R1, R0, #2
    e.write_thumb16(0x106, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 16u);
    ASSERT_EQ(e.cpu.r[1], 4u);
}

void test_thumb16_and_orr_eor() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #0xFF; MOV R1, #0x0F
    // AND R0, R1 → R0 = 0x0F
    // MOV R2, #0xF0; ORR R0, R2 → R0 = 0xFF
    // MOV R3, #0x55; EOR R0, R3 → R0 = 0xAA
    // Data Processing: AND = 0100000000|Rm|Rd, ORR = 0100001100|Rm|Rd, EOR = 0100000001|Rm|Rd
    e.write_thumb16(0x100, 0x20FF); // MOV R0, #0xFF
    e.write_thumb16(0x102, 0x210F); // MOV R1, #0x0F
    e.write_thumb16(0x104, 0x4008); // AND R0, R1  (4000 | Rm<<3 | Rd)
    e.write_thumb16(0x106, 0x22F0); // MOV R2, #0xF0
    e.write_thumb16(0x108, 0x4310); // ORR R0, R2  (4300 | Rm<<3 | Rd)
    e.write_thumb16(0x10A, 0x2355); // MOV R3, #0x55
    e.write_thumb16(0x10C, 0x4058); // EOR R0, R3  (4040 | Rm<<3 | Rd)
    e.write_thumb16(0x10E, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 0xAAu);
}

void test_thumb16_add_imm8() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R5, #100; ADD R5, #50; SUB R5, #25; BKPT
    e.write_thumb16(0x100, 0x2564); // MOV R5, #100
    e.write_thumb16(0x102, 0x3532); // ADD R5, #50
    e.write_thumb16(0x104, 0x3D19); // SUB R5, #25
    e.write_thumb16(0x106, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[5], 125u);
}

void test_thumb16_branch() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // B +4 (skip next instr); MOV R0, #99 (skipped); MOV R0, #42; BKPT
    // B <imm11>: 11100|offset11
    // B +4: offset from PC+4 to target. PC at 0x100, PC+4=0x104, target=0x104
    // offset = (0x104 - 0x104) / 2 = 0 → encoding: E000
    // Actually: skip one 16-bit instr means target = 0x104. offset = (target-(pc+4))/2 = 0
    // Wait. B unconditional: if at 0x100, PC+4=0x104. To reach 0x104, offset=0 → E000
    // That means no skip. Let me encode skip of MOV R0, #99 at 0x102:
    // B to 0x104: offset = (0x104 - (0x100+4))/2 = 0 → E000. No, that reaches 0x104.
    // MOV R0, #99 at 0x102, MOV R0, #42 at 0x104.
    // B at 0x100 to 0x104: offset = (0x104 - (0x100 + 4)) / 2 = 0 → 0xE000
    // Hmm that lands on 0x104 which is MOV R0, #42. Good.
    e.write_thumb16(0x100, 0xE000); // B +0 (skip to 0x104)
    e.write_thumb16(0x102, 0x2063); // MOV R0, #99 (skipped)
    e.write_thumb16(0x104, 0x202A); // MOV R0, #42
    e.write_thumb16(0x106, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 42u);
}

void test_thumb16_conditional_branch() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #5; CMP R0, #5; BEQ +2; MOV R1, #0; MOV R1, #1; BKPT
    // BEQ: 1101|0000|offset8
    // At 0x104, target=0x108, offset=(0x108-(0x104+4))/2 = 0 → D000
    e.write_thumb16(0x100, 0x2005); // MOV R0, #5
    e.write_thumb16(0x102, 0x2805); // CMP R0, #5
    e.write_thumb16(0x104, 0xD000); // BEQ +0 → 0x108
    e.write_thumb16(0x106, 0x2100); // MOV R1, #0 (skipped)
    e.write_thumb16(0x108, 0x2101); // MOV R1, #1
    e.write_thumb16(0x10A, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[1], 1u);
}

void test_thumb16_bne_not_taken() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #5; CMP R0, #5; BNE +2; MOV R1, #42; BKPT
    // BNE: 1101|0001|offset8
    e.write_thumb16(0x100, 0x2005); // MOV R0, #5
    e.write_thumb16(0x102, 0x2805); // CMP R0, #5 → Z=1
    e.write_thumb16(0x104, 0xD100); // BNE +0 → not taken (Z=1)
    e.write_thumb16(0x106, 0x212A); // MOV R1, #42 (executed)
    e.write_thumb16(0x108, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[1], 42u);
}

void test_thumb16_mvn_neg() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #0; MVN R1, R0 → R1=0xFFFFFFFF; NEG R2, R0 → R2=0; BKPT
    // MVN: 0100001111|Rm|Rd = 43C0 | Rm<<3 | Rd
    // NEG (RSB Rd, Rn, #0): 0100001001|Rn|Rd = 4240 | Rn<<3 | Rd
    e.write_thumb16(0x100, 0x2000); // MOV R0, #0
    e.write_thumb16(0x102, 0x43C1); // MVN R1, R0
    e.write_thumb16(0x104, 0x4242); // NEG R2, R0
    e.write_thumb16(0x106, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[1], 0xFFFFFFFF);
    ASSERT_EQ(e.cpu.r[2], 0u);
}

void test_thumb16_mul() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // MOV R0, #7; MOV R1, #6; MUL R0, R1; BKPT
    // MUL: 0100001101|Rn|Rd = 4340 | Rn<<3 | Rd
    e.write_thumb16(0x100, 0x2007); // MOV R0, #7
    e.write_thumb16(0x102, 0x2106); // MOV R1, #6
    e.write_thumb16(0x104, 0x4348); // MUL R0, R1, R0  (MUL Rd, Rn: Rd = Rd * Rn)
    e.write_thumb16(0x106, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 42u);
}

void test_thumb16_adr() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // ADR R0, [PC, #0] → R0 = Align(PC+4, 4) + 0
    // ADR: 10100|Rd|imm8 = A000 | Rd<<8 | imm8
    // At 0x100, PC read = 0x104, Align(0x104,4) = 0x104, +0 = 0x104
    e.write_thumb16(0x100, 0xA000); // ADR R0, [PC, #0]
    e.write_thumb16(0x102, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 0x104u);
}

void test_thumb16_add_sp_imm() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // ADD SP, SP, #16 (B004); SUB SP, SP, #8 (B082); BKPT
    e.write_thumb16(0x100, 0xB004); // ADD SP, #16
    e.write_thumb16(0x102, 0xB082); // SUB SP, #8
    e.write_thumb16(0x104, 0xBE00); // BKPT

    e.cpu.reset();
    uint32_t initial_sp = e.cpu.r[13];
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[13], initial_sp + 8);
}

void test_wfe_sev() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);

    // SEV; WFE; BKPT
    // SEV = 0xBF40, WFE = 0xBF20
    e.write_thumb16(0x100, 0xBF40); // SEV
    e.write_thumb16(0x102, 0xBF20); // WFE
    e.write_thumb16(0x104, 0xBE00); // BKPT

    e.cpu.reset();
    e.cpu.run(100);
    // SEV sets event_reg, WFE consumes it and continues (no sleep)
    ASSERT(e.cpu.halted);
    ASSERT(!e.cpu.wfi); // Should not be sleeping
}

void test_nested_exception() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);
    e.set_vector(21, 0x201); // IRQ 5
    e.set_vector(22, 0x301); // IRQ 6

    // Code: NOP, NOP, BKPT
    e.write_thumb16(0x100, 0xBF00);
    e.write_thumb16(0x102, 0xBF00);
    e.write_thumb16(0x104, 0xBE00);

    // IRQ 5 handler: NOP, BX LR
    e.write_thumb16(0x200, 0xBF00);
    e.write_thumb16(0x202, 0x4770);

    // IRQ 6 handler: BX LR
    e.write_thumb16(0x300, 0x4770);

    e.cpu.reset();

    // Enable IRQ 5 (priority 0x40) and IRQ 6 (priority 0x20 = higher)
    e.nvic.write(0x100, Width::Word, (1u << 5) | (1u << 6));
    // IPR[4..7]: IRQ5 at byte 1, IRQ6 at byte 2
    e.nvic.write(0x404, Width::Word, 0x00204000); // IPR[5]=0x40, IPR[6]=0x20

    // Pend IRQ 5
    e.nvic.set_pending(5);
    e.cpu.step(); // NOP at 0x100
    e.cpu.check_exceptions();
    ASSERT_EQ(e.cpu.r[15], 0x200u);
    ASSERT_EQ(e.cpu.active_exception(), 21u);

    // Now pend IRQ 6 (higher priority) during IRQ 5 handler
    e.nvic.set_pending(6);
    e.cpu.step(); // NOP at 0x200
    e.cpu.check_exceptions();
    // IRQ 6 should preempt IRQ 5
    ASSERT_EQ(e.cpu.r[15], 0x300u);
    ASSERT_EQ(e.cpu.active_exception(), 22u);

    // Return from IRQ 6
    e.cpu.step(); // BX LR
    // Back to IRQ 5 handler
    ASSERT_EQ(e.cpu.r[15], 0x202u);
    ASSERT_EQ(e.cpu.active_exception(), 21u);

    // Return from IRQ 5
    e.cpu.step(); // BX LR
    ASSERT_EQ(e.cpu.r[15], 0x102u);
    ASSERT(!e.cpu.in_handler_mode());
}

void test_svc_number() {
    MiniEnv e;
    e.set_vector(0, 0x20010000);
    e.set_vector(1, 0x101);
    e.set_vector(11, 0x201); // SVCall

    // SVC #42
    e.write_thumb16(0x100, 0xDF2A); // SVC #42
    e.write_thumb16(0x102, 0xBE00);
    e.write_thumb16(0x200, 0x4770); // BX LR

    e.cpu.reset();
    e.cpu.step(); // SVC
    e.cpu.check_exceptions();
    ASSERT(e.cpu.in_handler_mode());
    ASSERT_EQ(e.cpu.active_exception(), 11u);
}

// ============================================================
// 32-bit Thumb instruction tests
// ============================================================

void test_thumb32_bl() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // BL to subroutine at 0x200
    // BL encoding: hw1 = 11110|S|imm10, hw2 = 11|J1|1|J2|imm11
    // Target 0x200, PC at 0x100. offset = 0x200 - (0x100+4) = 0xFC
    // imm32 = S:I1:I2:imm10:imm11:0
    // offset = 0xFC = 0b11111100, S=0, imm10 = 0b0000000000, imm11 = 0b00001111110
    // Wait, let me compute properly:
    // offset = target - (PC+4) = 0x200 - 0x104 = 0xFC
    // S=0 (positive), I1 = !(J1 xor S) → J1=1 for I1=1
    // I2 = !(J2 xor S) → J2=1 for I2=1
    // imm10 = (offset >> 12) & 0x3FF = 0
    // imm11 = (offset >> 1) & 0x7FF = 0x7E
    // hw1 = 0xF000 | (S<<10) | imm10 = 0xF000
    // hw2 = 0xD000 | (J1<<13) | (J2<<11) | imm11 = 0xD000 | 0x2000 | 0x0800 | 0x7E = 0xF87E
    e.write_thumb32(0x100, 0xF000, 0xF87E); // BL +0xFC → 0x200
    e.write_thumb16(0x104, 0xBE00); // BKPT (return here)

    // Subroutine at 0x200: MOV R0, #99; BX LR
    e.write_thumb16(0x200, 0x2063); // MOV R0, #99
    e.write_thumb16(0x202, 0x4770); // BX LR

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 99u);
    // LR should have been set to 0x105 (return addr with thumb bit)
}

void test_thumb32_movw() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // MOVW R0, #0x1234
    // Encoding: hw1 = 11110|i|10|0|1|0|0|imm4, hw2 = 0|imm3|Rd|imm8
    // imm16 = imm4:i:imm3:imm8 = 0x1234
    // imm4 = 1, i = 0, imm3 = 0b010, imm8 = 0x34
    // hw1 = 0xF240 | (imm4) | (i << 10) = 0xF241
    // hw2 = (imm3 << 12) | (Rd << 8) | imm8 = 0x2034
    e.write_thumb32(0x100, 0xF241, 0x2034); // MOVW R0, #0x1234
    e.write_thumb16(0x104, 0xBE00);

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 0x1234u);
}

void test_thumb32_movw_movt() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // MOVW R0, #0x5678 → R0 = 0x5678
    // MOVT R0, #0x1234 → R0 = 0x12345678
    // MOVW: imm16=0x5678: imm4=5, i=0, imm3=110, imm8=0x78
    // hw1 = 0xF245, hw2 = 0x6078
    e.write_thumb32(0x100, 0xF245, 0x6078); // MOVW R0, #0x5678
    // MOVT: 11110|i|10|1|1|0|0|imm4, 0|imm3|Rd|imm8
    // imm16=0x1234: imm4=1, i=0, imm3=010, imm8=0x34
    // hw1 = 0xF2C1, hw2 = 0x2034
    e.write_thumb32(0x104, 0xF2C1, 0x2034); // MOVT R0, #0x1234
    e.write_thumb16(0x108, 0xBE00);

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[0], 0x12345678u);
}

void test_thumb32_ldr_w_str_w() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // MOVW R0, #0x0100; MOVT R0, #0x2000 → R0 = 0x20000100
    // MOV R1, #42
    // STR.W R1, [R0, #0]
    // LDR.W R2, [R0, #0]
    // BKPT
    e.write_thumb32(0x100, 0xF240, 0x1000); // MOVW R0, #0x0100
    e.write_thumb32(0x104, 0xF2C2, 0x0000); // MOVT R0, #0x2000
    e.write_thumb16(0x108, 0x212A);         // MOV R1, #42
    // STR.W R1, [R0, #0]: 11111|000|1|1|0|0|Rn|Rt|imm12
    // hw1 = 0xF8C0, hw2 = 0x1000 (Rt=1, imm12=0)
    e.write_thumb32(0x10A, 0xF8C0, 0x1000); // STR.W R1, [R0, #0]
    // LDR.W R2, [R0, #0]: 11111|000|1|1|0|1|Rn|Rt|imm12
    // hw1 = 0xF8D0, hw2 = 0x2000
    e.write_thumb32(0x10E, 0xF8D0, 0x2000); // LDR.W R2, [R0, #0]
    e.write_thumb16(0x112, 0xBE00);

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[2], 42u);
}

void test_thumb32_udiv_sdiv() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // MOV R0, #100; MOV R1, #7
    // UDIV R2, R0, R1 → R2 = 14
    // UDIV: 11111|01|1|1|0|1|1|Rn hw2 = 1111|Rd|1111|Rm
    // hw1 = 0xFBB0, hw2 = 0xF2F1
    e.write_thumb16(0x100, 0x2064); // MOV R0, #100
    e.write_thumb16(0x102, 0x2107); // MOV R1, #7
    e.write_thumb32(0x104, 0xFBB0, 0xF2F1); // UDIV R2, R0, R1
    e.write_thumb16(0x108, 0xBE00);

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.r[2], 14u);
}

void test_pendsv_handler() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);
    e.set_vector(14, 0x201); // PendSV

    e.write_thumb16(0x100, 0xBF00); // NOP
    e.write_thumb16(0x102, 0xBE00); // BKPT
    e.write_thumb16(0x200, 0x4770); // BX LR

    e.cpu.reset();

    // Set PendSV via ICSR
    e.nvic.write(0xD04, Width::Word, 1u << 28); // PENDSVSET
    e.cpu.step(); // NOP
    e.cpu.check_exceptions();

    ASSERT(e.cpu.in_handler_mode());
    ASSERT_EQ(e.cpu.active_exception(), 14u);

    // Return
    e.cpu.step(); // BX LR
    ASSERT(!e.cpu.in_handler_mode());
    ASSERT_EQ(e.cpu.r[15], 0x102u);
}

void test_faultmask_blocks_all() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);
    e.set_vector(21, 0x301); // IRQ 5

    e.write_thumb16(0x100, 0xBF00);
    e.write_thumb16(0x102, 0xBE00);
    e.write_thumb16(0x300, 0x4770);

    e.cpu.reset();
    e.cpu.faultmask = 1;
    e.nvic.write(0x100, Width::Word, 1u << 5);
    e.nvic.set_pending(5);

    e.cpu.step();
    e.cpu.check_exceptions();
    // FAULTMASK blocks everything except NMI
    ASSERT(!e.cpu.in_handler_mode());
}

// ============================================================
// VFP instruction tests
// ============================================================

void test_vfp_vmov_imm() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);

    // Enable FPU: CPACR |= 0x00F00000
    e.nvic.write(0xD88, Width::Word, 0x00F00000);

    // VMOV.F32 S0, #1.0
    // Encoding: VFP imm = 0x70 for 1.0 (a=0, b=1, c=1, d=1, e=0, f=0, g=0, h=0)
    // cond=1110, 11101|D|11|imm4H|Vd|101|0|0000|imm4L
    // S0: Vd=0, D=0
    // 1.0 = 0 01111111 00000000000000000000000 → abcdefgh = 01110000 = 0x70
    // imm4H = 0111, imm4L = 0000
    // hw1 = 0xEEB7, hw2 = 0x0A00
    e.write_thumb32(0x100, 0xEEB7, 0x0A00); // VMOV.F32 S0, #1.0
    e.write_thumb16(0x104, 0xBE00);

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    // S0 should contain 1.0 = 0x3F800000
    ASSERT_EQ(e.cpu.s[0], 0x3F800000u);
}

void test_vfp_vadd() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);
    e.nvic.write(0xD88, Width::Word, 0x00F00000);

    // VMOV.F32 S0, #2.0; VMOV.F32 S1, #3.0; VADD.F32 S2, S0, S1
    // 2.0 = 0 10000000 000... → abcdefgh = 00000000 → imm4H=0, imm4L=0...
    // Actually 2.0 in VFP imm8: a=0, B=1(inverted b), cdefgh=000000 → a=0,b=0,c-h=0
    // That's 0x00. Let me use VMOV from core reg instead.
    // VMOV S0, R0: 11101110000|0|Vn|Rt|1010|N|001|0000
    // Easier: set S registers directly and test VADD
    e.cpu.reset();

    // Set S0=2.0, S1=3.0 directly
    float f2 = 2.0f, f3 = 3.0f;
    memcpy(&e.cpu.s[0], &f2, 4);
    memcpy(&e.cpu.s[1], &f3, 4);

    // VADD.F32 S2, S0, S1
    // 1110|11100|D|11|Vn|Vd|101|0|N|0|M|0|Vm
    // S2=Vd:D=1:0, S0=Vn:N=0:0, S1=Vm:M=0:1
    // Wait, single precision: Sd = D:Vd, Sn = N:Vn, Sm = M:Vm
    // S0: D=0,Vd=0; S1: N=0,Vn=0,M=1,Vm=0...
    // This encoding is complex. Let me just use a simpler approach:
    // Write the instruction bytes directly.
    // VADD.F32 S2, S0, S1: 0xEE301A20
    // In Thumb: hw1 = 0xEE30, hw2 = 0x1A20
    e.write_thumb32(0x100, 0xEE30, 0x1A20); // VADD.F32 S2, S0, S1
    e.write_thumb16(0x104, 0xBE00);

    e.cpu.xpsr = (1u << 24); // T bit
    e.cpu.r[15] = 0x100;
    e.cpu.run(100);
    ASSERT(e.cpu.halted);

    float result;
    memcpy(&result, &e.cpu.s[2], 4);
    ASSERT_EQ((int)(result * 10), 50); // 5.0 * 10 = 50
}

void test_vfp_vmul() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    float f4 = 4.0f, f5 = 5.0f;
    memcpy(&e.cpu.s[0], &f4, 4);
    memcpy(&e.cpu.s[1], &f5, 4);

    // VMUL.F32 S2, S0, S1: 0xEE201A20
    e.write_thumb32(0x100, 0xEE20, 0x1A20); // VMUL.F32 S2, S0, S1
    e.write_thumb16(0x104, 0xBE00);

    e.cpu.xpsr = (1u << 24);
    e.cpu.r[15] = 0x100;
    e.cpu.run(100);
    ASSERT(e.cpu.halted);

    float result;
    memcpy(&result, &e.cpu.s[2], 4);
    ASSERT_EQ((int)(result * 10), 200); // 20.0 * 10 = 200
}

void test_vfp_vldr_vstr() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // Store 3.14 at SRAM address 0x20000100
    float pi = 3.14f;
    uint32_t pi_bits;
    memcpy(&pi_bits, &pi, 4);
    e.ram.data()[0x100] = pi_bits & 0xFF;
    e.ram.data()[0x101] = (pi_bits >> 8) & 0xFF;
    e.ram.data()[0x102] = (pi_bits >> 16) & 0xFF;
    e.ram.data()[0x103] = (pi_bits >> 24) & 0xFF;

    // Load address into R0
    // MOVW R0, #0x0100: imm4=0,i=0,imm3=001,Rd=0,imm8=0x00
    e.write_thumb32(0x100, 0xF240, 0x1000); // MOVW R0, #0x0100
    e.write_thumb32(0x104, 0xF2C2, 0x0000); // MOVT R0, #0x2000
    // VLDR S0, [R0, #0]: 1110|1101|1|D|01|Rn|Vd|1010|imm8
    // S0: Vd=0, D=0. Rn=R0. imm8=0.
    // hw1 = 0xED90, hw2 = 0x0A00
    e.write_thumb32(0x108, 0xED90, 0x0A00); // VLDR S0, [R0]
    // VSTR S0, [R0, #4]: imm8 = 1 (offset = 4)
    // hw1 = 0xED80, hw2 = 0x0A01
    e.write_thumb32(0x10C, 0xED80, 0x0A01); // VSTR S0, [R0, #4]
    e.write_thumb16(0x110, 0xBE00);

    e.cpu.xpsr = (1u << 24);
    e.cpu.r[15] = 0x100;
    e.cpu.run(100);
    ASSERT(e.cpu.halted);

    // S0 should have pi
    ASSERT_EQ(e.cpu.s[0], pi_bits);
    // SRAM at 0x104 should also have pi
    uint32_t stored = e.ram.data()[0x104] | (e.ram.data()[0x105] << 8) |
                      (e.ram.data()[0x106] << 16) | (e.ram.data()[0x107] << 24);
    ASSERT_EQ(stored, pi_bits);
}

void test_vfp_vcmp() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    float f1 = 1.0f, f2 = 2.0f;
    memcpy(&e.cpu.s[0], &f1, 4);
    memcpy(&e.cpu.s[1], &f2, 4);

    // VCMP.F32 S0, S1: 1110|11101|D|11|0100|Vd|101|0|0|1|M|0|Vm
    // S0 vs S1: Vd=0,D=0, Vm=0,M=1
    // hw1 = 0xEEB4, hw2 = 0x0A60
    e.write_thumb32(0x100, 0xEEB4, 0x0A60); // VCMP.F32 S0, S1
    // VMRS APSR_nzcv, FPSCR: 0xEEF1 0xFA10
    e.write_thumb32(0x104, 0xEEF1, 0xFA10);
    e.write_thumb16(0x108, 0xBE00);

    e.cpu.xpsr = (1u << 24);
    e.cpu.r[15] = 0x100;
    e.cpu.run(100);
    ASSERT(e.cpu.halted);

    // 1.0 < 2.0: N=1, Z=0, C=0, V=0
    ASSERT(e.cpu.flag_n());
    ASSERT(!e.cpu.flag_z());
}

void test_vmov_core_to_fpu() {
    MiniEnv e;
    e.set_vector(0, 0x2000FF00);
    e.set_vector(1, 0x101);
    e.nvic.write(0xD88, Width::Word, 0x00F00000);

    // MOV R0, #42; VMOV S0, R0
    // VMOV S0, R0: 1110|1110|000|0|Vn=0|Rt=0|1010|N=0|0010000
    // hw1 = 0xEE00, hw2 = 0x0A10
    e.write_thumb16(0x100, 0x202A); // MOV R0, #42
    e.write_thumb32(0x102, 0xEE00, 0x0A10); // VMOV S0, R0
    e.write_thumb16(0x106, 0xBE00);

    e.cpu.reset();
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
    ASSERT_EQ(e.cpu.s[0], 42u);
}

// Helper: set single-precision register
void set_sf(CPU& cpu, int reg, float v) { memcpy(&cpu.s[reg], &v, 4); }
float get_sf(CPU& cpu, int reg) { float v; memcpy(&v, &cpu.s[reg], 4); return v; }

// Helper: run a single VFP instruction from given S-register state
void run_vfp(MiniEnv& e, uint16_t hw1, uint16_t hw2) {
    e.write_thumb32(0x100, hw1, hw2);
    e.write_thumb16(0x104, 0xBE00); // BKPT
    e.cpu.xpsr = (1u << 24);
    e.cpu.r[15] = 0x100;
    e.cpu.halted = false;
    e.cpu.run(100);
    ASSERT(e.cpu.halted);
}

// === VFPv4 Fused Multiply-Add (single-precision, C++ tests) ===

void test_vfp_vfma() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    // VFMA.F32 S0, S2, S4: Sd = Sd + Sn*Sm
    set_sf(e.cpu, 0, 1.0f); set_sf(e.cpu, 2, 2.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEEA1, 0x0A02); // VFMA.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), 70); // 1+2*3=7
}

void test_vfp_vfms() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    // VFMS.F32 S0, S2, S4: Sd = Sd - Sn*Sm
    set_sf(e.cpu, 0, 10.0f); set_sf(e.cpu, 2, 2.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEEA1, 0x0A42); // VFMS.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), 40); // 10-2*3=4
}

void test_vfp_vfnma() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    // VFNMA.F32 S0, S2, S4: Sd = -(Sd + Sn*Sm)
    set_sf(e.cpu, 0, 1.0f); set_sf(e.cpu, 2, 2.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEE91, 0x0A42); // VFNMA.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), -70); // -(1+6)=-7
}

void test_vfp_vfnms() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    // VFNMS.F32 S0, S2, S4: Sd = -(Sd - Sn*Sm)
    set_sf(e.cpu, 0, 10.0f); set_sf(e.cpu, 2, 2.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEE91, 0x0A02); // VFNMS.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), -40); // -(10-6)=-4
}

// === VNMLA/VNMLS (single-precision, C++ tests) ===

void test_vfp_vnmla() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    // VNMLA.F32 S0, S2, S4: Sd = -(Sd + Sn*Sm)
    set_sf(e.cpu, 0, 1.0f); set_sf(e.cpu, 2, 2.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEE11, 0x0A42); // VNMLA.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), -70); // -(1+6)=-7
}

void test_vfp_vnmls() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    // VNMLS.F32 S0, S2, S4: Sd = -(Sd - Sn*Sm)
    set_sf(e.cpu, 0, 10.0f); set_sf(e.cpu, 2, 2.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEE11, 0x0A02); // VNMLS.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), -40); // -(10-6)=-4
}

void test_vfp_vsub() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    set_sf(e.cpu, 2, 5.0f); set_sf(e.cpu, 4, 2.0f);
    run_vfp(e, 0xEE31, 0x0A42); // VSUB.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), 30); // 5-2=3
}

void test_vfp_vdiv() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    set_sf(e.cpu, 2, 15.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEE81, 0x0A02); // VDIV.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), 50); // 15/3=5
}

void test_vfp_vnmul() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    set_sf(e.cpu, 2, 3.0f); set_sf(e.cpu, 4, 5.0f);
    run_vfp(e, 0xEE21, 0x0A42); // VNMUL.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), -150); // -(3*5)=-15
}

void test_vfp_vmla_vmls() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();
    // VMLA: S0 = S0 + S2*S4 = 1 + 2*3 = 7
    set_sf(e.cpu, 0, 1.0f); set_sf(e.cpu, 2, 2.0f); set_sf(e.cpu, 4, 3.0f);
    run_vfp(e, 0xEE01, 0x0A02); // VMLA.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), 70);

    // VMLS: S0 = S0 - S2*S4 = 7 - 2*3 = 1
    run_vfp(e, 0xEE01, 0x0A42); // VMLS.F32 S0, S2, S4
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), 10);
}

// === Double-precision (C++ only — not assemblable with fpv4-sp-d16) ===

void test_vfp_double_arith() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // VADD.F64 D0, D1, D2: 2.0 + 3.0 = 5.0
    e.cpu.set_df(1, 2.0); e.cpu.set_df(2, 3.0);
    run_vfp(e, 0xEE31, 0x0B02); // VADD.F64 D0, D1, D2
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 50);

    // VSUB.F64 D0, D1, D2: 2.0 - 3.0 = -1.0
    run_vfp(e, 0xEE31, 0x0B42); // VSUB.F64 D0, D1, D2
    ASSERT_EQ((int)(e.cpu.df(0) * 10), -10);

    // VMUL.F64 D0, D1, D2: 2.0 * 3.0 = 6.0
    run_vfp(e, 0xEE21, 0x0B02);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 60);

    // VDIV.F64 D0, D1, D2: 2.0 / 3.0 ≈ 0.6667
    e.cpu.set_df(1, 10.0); e.cpu.set_df(2, 4.0);
    run_vfp(e, 0xEE81, 0x0B02);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 25); // 10/4=2.5
}

void test_vfp_double_fma() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // VFMA.F64 D0, D1, D2: D0 = D0 + D1*D2 = 1 + 2*3 = 7
    e.cpu.set_df(0, 1.0); e.cpu.set_df(1, 2.0); e.cpu.set_df(2, 3.0);
    run_vfp(e, 0xEEA1, 0x0B02);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 70);

    // VFMS.F64 D0, D1, D2: D0 = D0 - D1*D2 = 7 - 2*3 = 1
    run_vfp(e, 0xEEA1, 0x0B42);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 10);

    // VFNMA.F64 D0, D1, D2: D0 = -(D0 + D1*D2) = -(1+6) = -7
    run_vfp(e, 0xEE91, 0x0B42);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), -70);

    // VFNMS.F64 D0, D1, D2: D0 = -(D0 - D1*D2) = -(-7-6) = 13
    run_vfp(e, 0xEE91, 0x0B02);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 130);
}

void test_vfp_double_negmul() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // VNMUL.F64 D0, D1, D2: -(2*3) = -6
    e.cpu.set_df(1, 2.0); e.cpu.set_df(2, 3.0);
    run_vfp(e, 0xEE21, 0x0B42);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), -60);

    // VMLA.F64 D0, D1, D2: D0 = D0 + D1*D2 = -6 + 6 = 0
    run_vfp(e, 0xEE01, 0x0B02);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 0);

    // VMLS.F64 D0, D1, D2: D0 = D0 - D1*D2 = 0 - 6 = -6
    run_vfp(e, 0xEE01, 0x0B42);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), -60);

    // VNMLA.F64 D0, D1, D2: D0 = -(D0 + D1*D2) = -(-6+6) = 0
    e.cpu.set_df(0, -6.0);
    run_vfp(e, 0xEE11, 0x0B42);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 0);

    // VNMLS.F64 D0, D1, D2: D0 = -(D0 - D1*D2) = -(0-6) = 6
    run_vfp(e, 0xEE11, 0x0B02);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 60);
}

void test_vfp_double_misc() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // VMOV.F64 D0, D1
    e.cpu.set_df(1, 3.14);
    run_vfp(e, 0xEEB0, 0x0B41);
    ASSERT_EQ((int)(e.cpu.df(0) * 100), 314);

    // VNEG.F64 D0, D1: -(3.14) = -3.14
    run_vfp(e, 0xEEB1, 0x0B41);
    ASSERT_EQ((int)(e.cpu.df(0) * 100), -314);

    // VABS.F64 D0, D1: abs(D1) — set D1=-5.0
    e.cpu.set_df(1, -5.0);
    run_vfp(e, 0xEEB0, 0x0BC1);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 50);

    // VSQRT.F64 D0, D1: sqrt(9.0) = 3.0
    e.cpu.set_df(1, 9.0);
    run_vfp(e, 0xEEB1, 0x0BC1);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 30);

    // VCMP.F64 D0(3.0), D1(9.0): 3 < 9 → N=1
    run_vfp(e, 0xEEB4, 0x0B41);
    ASSERT(e.cpu.fpscr & 0x80000000); // N flag

    // VCMP.F64 D0, D0: equal → Z=1,C=1
    e.cpu.set_df(1, 3.0); // D1=D0=3.0
    run_vfp(e, 0xEEB4, 0x0B41);
    // Actually D0=3.0 from sqrt, D1 was 9.0 then we set D1=3.0
    // Need to compare D0 with D1 which is now 3.0
    // D0 is 3.0 from sqrt result, D1 just set to 3.0
    ASSERT(e.cpu.fpscr & 0x40000000); // Z flag set (equal)

    // VCMPE.F64 D0, #0: 3.0 > 0 → C=1 only
    run_vfp(e, 0xEEB5, 0x0B40);
    ASSERT(e.cpu.fpscr & 0x20000000);   // C flag
    ASSERT(!(e.cpu.fpscr & 0x80000000)); // not N
    ASSERT(!(e.cpu.fpscr & 0x40000000)); // not Z
}

void test_vfp_double_cvt() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // VCVT.F64.S32 D0, S2: signed int→double
    e.cpu.s[2] = (uint32_t)(int32_t)-42; // S2 = -42 as int
    run_vfp(e, 0xEEB8, 0x0BC1);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), -420);

    // VCVT.F64.U32 D0, S2: unsigned int→double
    e.cpu.s[2] = 200;
    run_vfp(e, 0xEEB8, 0x0B41);
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 2000);

    // VCVT.S32.F64 S0, D1: double→signed int (toward zero)
    e.cpu.set_df(1, -7.9);
    run_vfp(e, 0xEEBD, 0x0BC1);
    ASSERT_EQ(e.cpu.s[0], (uint32_t)(int32_t)-7);

    // VCVT.U32.F64 S0, D1: double→unsigned int
    e.cpu.set_df(1, 42.7);
    run_vfp(e, 0xEEBC, 0x0BC1);
    ASSERT_EQ(e.cpu.s[0], 42u);
}

void test_vfp_cvt_f64_f32() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // VCVT.F64.F32 D0, S2: promote single→double
    // Encoding: opc1=1011 D=0 opc2=0111 Vd=0 1011 opc3=11 M Vm
    // S2: Vm=1, M=0 → hw2 has M=0, Vm=1
    // hw1=EEB7 (1110 1110 1011 0111), hw2=0AC1 → wait that's single
    // For F64.F32: result is double (cp=11), source is single
    // Actually VCVT.F64.F32 Dd, Sm: is in misc_single with opc2=7, opc3[0]=1
    // But the result goes to a double register...
    // ARM ARM: VCVT.F64.F32 is encoded with cp=10 (single) in the instruction,
    // but writes to double register Dd
    // hw1 = 0xEEB7, hw2 = 0x0AC0 | M<<5 | Vm
    // S2: Vm=1, M=0 → hw2 = 0x0AC1
    set_sf(e.cpu, 2, 3.0f);
    run_vfp(e, 0xEEB7, 0x0AC1); // VCVT.F64.F32 D0, S2
    ASSERT_EQ((int)(e.cpu.df(0) * 10), 30);

    // VCVT.F32.F64 S0, D1: demote double→single
    // opc2=0111, opc3=01, cp=11 (double source)
    // D1: Vm=1, M=0
    // hw1=EEB7 (opc2=7 in bits[19:16]), hw2=0B41
    // Wait: actually for F32.F64 it's in misc_double, opc2=7, opc3[0]=0
    // hw1 = 0xEEB7, hw2 = 0x0B41 → No, opc3=01 for F32.F64
    // Actually: opc3[0]=0 means F32.F64
    // hw2 = Vd<<12 | 1011 | 0 | 1 | M<<5 | 0 | Vm
    // S0: D=0, Vd=0 → hw2[15:12]=0, but Sd encoding is different
    // For VCVT.F32.F64 Sd, Dm: Sd = D:Vd, Dm = M:Vm
    // In the double misc context: the result is single
    // This is confusing. Let me just objdump verify.
    e.cpu.set_df(1, 7.0);
    run_vfp(e, 0xEEB7, 0x0B41); // VCVT.F32.F64 S0, D1
    ASSERT_EQ((int)(get_sf(e.cpu, 0) * 10), 70);
}

void test_vfp_vldr_vstr_double() {
    MiniEnv e;
    e.nvic.write(0xD88, Width::Word, 0x00F00000);
    e.cpu.reset();

    // VLDR.64 D0, [R0]: load 8 bytes into D0
    // First store 3.14 at RAM address 0x200
    e.cpu.set_df(0, 3.14);
    uint32_t lo = e.cpu.s[0], hi = e.cpu.s[1];
    e.ram.data()[0x200] = lo; e.ram.data()[0x201] = lo >> 8;
    e.ram.data()[0x202] = lo >> 16; e.ram.data()[0x203] = lo >> 24;
    e.ram.data()[0x204] = hi; e.ram.data()[0x205] = hi >> 8;
    e.ram.data()[0x206] = hi >> 16; e.ram.data()[0x207] = hi >> 24;

    // Clear D0, set R0 = 0x20000200
    e.cpu.set_df(0, 0.0);
    e.cpu.r[0] = 0x20000200;

    // VLDR.64 D0, [R0, #0]: hw1=ED90, hw2=0B00
    run_vfp(e, 0xED90, 0x0B00);
    ASSERT_EQ((int)(e.cpu.df(0) * 100), 314);

    // VSTR.64 D0, [R0, #8]: write to offset 8 = 0x20000208
    // hw1=ED80, hw2=0B02 (imm8=2 → offset=8)
    run_vfp(e, 0xED80, 0x0B02);
    // Verify by reading back
    uint32_t r_lo = e.ram.data()[0x208] | (e.ram.data()[0x209] << 8) |
                    (e.ram.data()[0x20A] << 16) | (e.ram.data()[0x20B] << 24);
    ASSERT_EQ(r_lo, lo);
}

int main() {
    RUN(test_reset);
    RUN(test_flags);
    RUN(test_it_state);
    RUN(test_sp_banking);
    RUN(test_step_bkpt);
    RUN(test_exception_entry_return);
    RUN(test_exception_context_preservation);
    RUN(test_wfi_halts_cpu);
    RUN(test_wfi_wakeup_on_pending_irq);
    RUN(test_run_batch);
    RUN(test_irq_preemption);
    RUN(test_primask_blocks_irq);
    RUN(test_basepri_masks_lower_priority);
    // Thumb-16 instruction tests
    RUN(test_thumb16_mov_add_sub);
    RUN(test_thumb16_cmp_flags);
    RUN(test_thumb16_ldr_str);
    RUN(test_thumb16_push_pop);
    RUN(test_thumb16_shifts);
    RUN(test_thumb16_and_orr_eor);
    RUN(test_thumb16_add_imm8);
    RUN(test_thumb16_branch);
    RUN(test_thumb16_conditional_branch);
    RUN(test_thumb16_bne_not_taken);
    RUN(test_thumb16_mvn_neg);
    RUN(test_thumb16_mul);
    RUN(test_thumb16_adr);
    RUN(test_thumb16_add_sp_imm);
    RUN(test_wfe_sev);
    RUN(test_nested_exception);
    RUN(test_svc_number);
    // 32-bit Thumb instruction tests
    RUN(test_thumb32_bl);
    RUN(test_thumb32_movw);
    RUN(test_thumb32_movw_movt);
    RUN(test_thumb32_ldr_w_str_w);
    RUN(test_thumb32_udiv_sdiv);
    RUN(test_pendsv_handler);
    RUN(test_faultmask_blocks_all);
    // VFP instruction tests
    RUN(test_vfp_vmov_imm);
    RUN(test_vfp_vadd);
    RUN(test_vfp_vmul);
    RUN(test_vfp_vldr_vstr);
    RUN(test_vfp_vcmp);
    RUN(test_vmov_core_to_fpu);
    // VFP single-precision complete coverage
    RUN(test_vfp_vsub);
    RUN(test_vfp_vdiv);
    RUN(test_vfp_vnmul);
    RUN(test_vfp_vmla_vmls);
    RUN(test_vfp_vnmla);
    RUN(test_vfp_vnmls);
    RUN(test_vfp_vfma);
    RUN(test_vfp_vfms);
    RUN(test_vfp_vfnma);
    RUN(test_vfp_vfnms);
    // VFP double-precision
    RUN(test_vfp_double_arith);
    RUN(test_vfp_double_fma);
    RUN(test_vfp_double_negmul);
    RUN(test_vfp_double_misc);
    RUN(test_vfp_double_cvt);
    RUN(test_vfp_cvt_f64_f32);
    RUN(test_vfp_vldr_vstr_double);
    TEST_REPORT();
}
