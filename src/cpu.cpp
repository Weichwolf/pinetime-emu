#include "cpu.hpp"
#include "device/nvic.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pt {

// ============================================================================
// Helpers
// ============================================================================

bool CPU::condition_passed(uint8_t cond) const {
    bool result;
    switch (cond >> 1) {
    case 0: result = flag_z(); break;                        // EQ/NE
    case 1: result = flag_c(); break;                        // CS/CC
    case 2: result = flag_n(); break;                        // MI/PL
    case 3: result = flag_v(); break;                        // VS/VC
    case 4: result = flag_c() && !flag_z(); break;           // HI/LS
    case 5: result = flag_n() == flag_v(); break;            // GE/LT
    case 6: result = flag_n() == flag_v() && !flag_z(); break; // GT/LE
    case 7: result = true; break;                            // AL
    default: result = true;
    }
    return (cond & 1) ? !result : result;
}

uint32_t CPU::add_with_carry(uint32_t a, uint32_t b, bool cin, bool& cout, bool& vout) {
    uint64_t sum = (uint64_t)a + b + (cin ? 1ULL : 0);
    uint32_t result = (uint32_t)sum;
    cout = sum > 0xFFFFFFFF;
    vout = ((a ^ result) & (b ^ result)) >> 31;
    return result;
}

uint32_t CPU::shift_c(uint32_t val, ShiftType type, uint8_t amount, bool& carry) {
    if (amount == 0) { carry = flag_c(); return val; }
    switch (type) {
    case ShiftType::LSL:
        carry = (val >> (32 - amount)) & 1;
        return val << amount;
    case ShiftType::LSR:
        carry = (val >> (amount - 1)) & 1;
        return val >> amount;
    case ShiftType::ASR:
        carry = ((int32_t)val >> (amount - 1)) & 1;
        return (uint32_t)((int32_t)val >> amount);
    case ShiftType::ROR:
        carry = (val >> (amount - 1)) & 1;
        return (val >> amount) | (val << (32 - amount));
    }
    return val;
}

uint32_t CPU::thumb_expand_imm_c(uint16_t imm12, bool& carry) {
    if ((imm12 >> 10) == 0) {
        uint8_t imm8 = imm12 & 0xFF;
        switch ((imm12 >> 8) & 3) {
        case 0: carry = flag_c(); return imm8;
        case 1: carry = flag_c(); return (imm8 << 16) | imm8;
        case 2: carry = flag_c(); return (imm8 << 24) | (imm8 << 8);
        case 3: carry = flag_c(); return (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8;
        }
    }
    uint32_t unrotated = 0x80 | (imm12 & 0x7F);
    uint8_t rot = (imm12 >> 7) & 0x1F;
    uint32_t result = (unrotated >> rot) | (unrotated << (32 - rot));
    carry = result >> 31;
    return result;
}

// Decode immediate shift: returns (ShiftType, amount)
static void decode_imm_shift(uint8_t type, uint8_t imm5, CPU::ShiftType& st, uint8_t& amount) {
    switch (type) {
    case 0: st = CPU::ShiftType::LSL; amount = imm5; break;
    case 1: st = CPU::ShiftType::LSR; amount = imm5 ? imm5 : 32; break;
    case 2: st = CPU::ShiftType::ASR; amount = imm5 ? imm5 : 32; break;
    case 3:
        if (imm5 == 0) { st = CPU::ShiftType::ROR; amount = 1; } // RRX
        else { st = CPU::ShiftType::ROR; amount = imm5; }
        break;
    }
}

// Read register with PC adjustment (PC reads as instruction + 4)
inline uint32_t CPU::read_reg(int n) const {
    if (n == 15) return (r[15] + 4) & ~1u;
    return r[n];
}

inline void CPU::write_reg(int n, uint32_t val) {
    if (n == 15) { branch_to(val); branch_taken_ = true; return; }
    if (n == 13) { set_sp(val); return; }
    r[n] = val;
}

// ============================================================================
// Reset
// ============================================================================

void CPU::reset() {
    memset(r.data(), 0, sizeof(r));
    xpsr = (1u << 24); // T bit = 1 (Thumb mode)
    primask = basepri = faultmask = control = 0;
    cycles = 0;
    halted = wfi = false;
    branch_taken_ = false;

    // Read initial SP and PC from vector table
    msp = bus_->read32(0x00000000) & ~3u;
    r[13] = msp;
    psp = 0;
    uint32_t reset_vector = bus_->read32(0x00000004);
    r[15] = reset_vector & ~1u;
}

// ============================================================================
// Exception handling
// ============================================================================

void CPU::exception_entry(uint32_t exception_num) {
    // Determine stack to use
    bool use_psp = !in_handler_mode() && (control & 2);
    uint32_t frame_sp = use_psp ? psp : msp;
    frame_sp -= 32; // 8 words

    // Push context: R0, R1, R2, R3, R12, LR, ReturnAddress, xPSR
    bus_->write32(frame_sp + 0,  r[0]);
    bus_->write32(frame_sp + 4,  r[1]);
    bus_->write32(frame_sp + 8,  r[2]);
    bus_->write32(frame_sp + 12, r[3]);
    bus_->write32(frame_sp + 16, r[12]);
    bus_->write32(frame_sp + 20, r[14]);
    bus_->write32(frame_sp + 24, r[15]); // Return address
    bus_->write32(frame_sp + 28, xpsr);

    // Update stack pointer used for frame
    if (use_psp) psp = frame_sp;
    else msp = frame_sp;

    // Set EXC_RETURN in LR (before changing mode)
    if (use_psp)
        r[14] = 0xFFFFFFFD; // Thread mode, PSP
    else if (in_handler_mode())
        r[14] = 0xFFFFFFF1; // Handler mode, MSP
    else
        r[14] = 0xFFFFFFF9; // Thread mode, MSP

    // Set active exception (IPSR is bits [8:0]) — now in handler mode
    xpsr = (xpsr & ~0x1FFu) | (exception_num & 0x1FF);

    // In handler mode, SP = MSP always
    r[13] = msp;
    set_it_state(0); // Clear IT state

    // Fetch vector
    // Fetch vector
    uint32_t vtor = nvic_ ? nvic_->get_vtor() : 0;
    uint32_t vector = bus_->read32(vtor + exception_num * 4);
    r[15] = vector & ~1u;
    branch_taken_ = true;

    // Mark external interrupt as active
    if (exception_num >= 16 && nvic_) {
        nvic_->set_active(exception_num - 16);
        nvic_->clear_pending(exception_num - 16);
    }
}

void CPU::exception_return(uint32_t exc_return) {
    // Determine return stack
    bool return_to_psp = exc_return & 4;
    uint32_t frame_sp = return_to_psp ? psp : msp;

    // Pop context
    r[0]  = bus_->read32(frame_sp + 0);
    r[1]  = bus_->read32(frame_sp + 4);
    r[2]  = bus_->read32(frame_sp + 8);
    r[3]  = bus_->read32(frame_sp + 12);
    r[12] = bus_->read32(frame_sp + 16);
    r[14] = bus_->read32(frame_sp + 20);
    uint32_t return_addr = bus_->read32(frame_sp + 24);
    uint32_t return_xpsr = bus_->read32(frame_sp + 28);

    frame_sp += 32;
    if (return_to_psp) psp = frame_sp;
    else msp = frame_sp;

    // Clear active exception before restoring xpsr
    uint32_t prev_exc = xpsr & 0x1FF;
    if (prev_exc >= 16 && nvic_)
        nvic_->clear_active(prev_exc - 16);

    // Restore xpsr (keep T bit set)
    xpsr = return_xpsr | (1u << 24);

    // Restore SP and CONTROL based on return mode
    if (exc_return & 8) {
        // Return to thread mode — update CONTROL.SPSEL from EXC_RETURN[2]
        if (exc_return & 4)
            control |= 2;   // SPSEL=1 (PSP)
        else
            control &= ~2u;  // SPSEL=0 (MSP)
        r[13] = (control & 2) ? psp : msp;
    } else {
        // Return to handler mode
        r[13] = msp;
    }

    r[15] = return_addr & ~1u;
    branch_taken_ = true;
    irq_pending = true;  // Execution priority changed, re-check
}

void CPU::check_exceptions() {
    if (!nvic_) return;

    // Determine current execution priority
    int exec_pri = 256; // Lowest priority (thread mode)
    uint16_t active_exc = xpsr & 0x1FF;
    if (active_exc) {
        if (active_exc >= 16)
            exec_pri = nvic_->priority(active_exc - 16);
        else
            exec_pri = nvic_->system_priority(active_exc);
    }

    // Check PRIMASK/BASEPRI
    int effective_basepri = 256;
    if (primask & 1) effective_basepri = 0; // Mask everything
    else if (basepri) effective_basepri = basepri;
    if (faultmask & 1) effective_basepri = -1;

    // Check system exceptions (PendSV = 14, SysTick = 15)
    for (int exc : {15, 14, 11}) { // SysTick, PendSV, SVCall
        if (nvic_->is_sys_pending(exc)) {
            int pri = nvic_->system_priority(exc);
            if (pri < exec_pri && pri < effective_basepri) {
                nvic_->clear_sys_pending(exc);
                exception_entry(exc);
                wfi = false;
                return;
            }
        }
    }

    // Check external interrupts
    int irq = nvic_->get_pending_irq();
    if (irq >= 0) {
        int pri = nvic_->priority(irq);
        if (pri < exec_pri && pri < effective_basepri) {
            exception_entry(irq + 16);
            wfi = false;
            return;
        }
    }
}

// ============================================================================
// Step / Run
// ============================================================================

void CPU::step() {
    if (halted) return;
    if (wfi) {
        check_exceptions();
        if (wfi) { cycles++; return; }
    }

    branch_taken_ = false;
    uint32_t inst_pc = r[15];

uint16_t hw1 = bus_->read16(r[15]);
    bool is32 = (hw1 >> 11) >= 0x1D;

    // IT block: check condition BEFORE execution
    bool was_in_it = in_it_block();
    bool skip = was_in_it && !condition_passed(it_cond());

    if (skip) {
        // Instruction skipped due to IT condition
    } else if (is32) {
        uint16_t hw2 = bus_->read16(r[15] + 2);
        exec_thumb32(hw1, hw2);
    } else {
        exec_thumb16(hw1);
    }

    if (!branch_taken_)
        r[15] += is32 ? 4 : 2;

    // Catch wild PC
    if (r[15] >= 0x80000 && r[15] < 0x20000000) {
        static int wild_count = 0;
        if (wild_count == 0) {
            uint16_t hw2_dbg = is32 ? bus_->read16(inst_pc + 2) : 0;
            fprintf(stderr, "[CPU] Wild PC=0x%08X from inst@0x%08X hw1=0x%04X hw2=0x%04X cy=%llu\n",
                    r[15], inst_pc, hw1, hw2_dbg, (unsigned long long)cycles);
            fprintf(stderr, "  R0=0x%X R1=0x%X R2=0x%X R3=0x%X R4=0x%X R5=0x%X R6=0x%X R7=0x%X\n",
                    r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7]);
            fprintf(stderr, "  R8=0x%X R9=0x%X R10=0x%X R11=0x%X R12=0x%X SP=0x%X LR=0x%X\n",
                    r[8],r[9],r[10],r[11],r[12],r[13],r[14]);
        }
        if (wild_count++ >= 2) { halted = true; return; }
    }

    // Advance IT state only if we were already in IT block before this instruction
    // (IT instruction itself sets up the block but should not be advanced)
    if (was_in_it) advance_it();
    cycles++;
}

void CPU::run(uint64_t target_cycles) {
    while (cycles < target_cycles && !halted) {
        if (nvic_ && nvic_->check_sysreset()) {
            fprintf(stderr, "[CPU] SYSRESETREQ at PC=0x%08X SP=0x%08X LR=0x%08X cy=%llu\n",
                    r[15], r[13], r[14], (unsigned long long)cycles);
            memset(r.data(), 0, sizeof(r));
            xpsr = (1u << 24);
            primask = basepri = faultmask = control = 0;
            wfi = false;
            branch_taken_ = false;
            msp = bus_->read32(0) & ~3u;
            r[13] = msp;
            psp = 0;
            r[15] = bus_->read32(4) & ~1u;
            continue;
        }
        if (irq_pending || wfi) {
            check_exceptions();
            irq_pending = false;
            if (wfi) { cycles++; continue; }
        }
        step();
    }
}

// ============================================================================
// 16-bit Thumb execution
// ============================================================================

void CPU::exec_thumb16(uint16_t inst) {
    bool set_flags = !in_it_block();

    switch (inst >> 11) {
    // ---- Shift by immediate ----
    case 0b00000: { // LSL Rd, Rm, #imm5
        uint8_t rd = inst & 7, rm = (inst >> 3) & 7;
        uint8_t imm5 = (inst >> 6) & 0x1F;
        bool carry;
        uint32_t result = shift_c(r[rm], ShiftType::LSL, imm5, carry);
        r[rd] = result;
        if (set_flags) set_nzc(result, imm5 ? carry : flag_c());
        break;
    }
    case 0b00001: { // LSR Rd, Rm, #imm5
        uint8_t rd = inst & 7, rm = (inst >> 3) & 7;
        uint8_t imm5 = (inst >> 6) & 0x1F;
        bool carry;
        uint32_t result = shift_c(r[rm], ShiftType::LSR, imm5 ? imm5 : 32, carry);
        r[rd] = result;
        if (set_flags) set_nzc(result, carry);
        break;
    }
    case 0b00010: { // ASR Rd, Rm, #imm5
        uint8_t rd = inst & 7, rm = (inst >> 3) & 7;
        uint8_t imm5 = (inst >> 6) & 0x1F;
        bool carry;
        uint32_t result = shift_c(r[rm], ShiftType::ASR, imm5 ? imm5 : 32, carry);
        r[rd] = result;
        if (set_flags) set_nzc(result, carry);
        break;
    }
    case 0b00011: { // ADD/SUB (register or 3-bit immediate)
        uint8_t rd = inst & 7, rn = (inst >> 3) & 7;
        uint8_t op = (inst >> 9) & 3;
        bool cout, vout;
        uint32_t result;
        switch (op) {
        case 0: // ADD Rd, Rn, Rm
            result = add_with_carry(r[rn], r[(inst >> 6) & 7], false, cout, vout);
            r[rd] = result;
            if (set_flags) set_nzcv(result, cout, vout);
            break;
        case 1: // SUB Rd, Rn, Rm
            result = add_with_carry(r[rn], ~r[(inst >> 6) & 7], true, cout, vout);
            r[rd] = result;
            if (set_flags) set_nzcv(result, cout, vout);
            break;
        case 2: { // ADD Rd, Rn, #imm3
            uint32_t imm3 = (inst >> 6) & 7;
            result = add_with_carry(r[rn], imm3, false, cout, vout);
            r[rd] = result;
            if (set_flags) set_nzcv(result, cout, vout);
            break;
        }
        case 3: { // SUB Rd, Rn, #imm3
            uint32_t imm3 = (inst >> 6) & 7;
            result = add_with_carry(r[rn], ~imm3, true, cout, vout);
            r[rd] = result;
            if (set_flags) set_nzcv(result, cout, vout);
            break;
        }
        }
        break;
    }
    // ---- Mov/Cmp/Add/Sub immediate ----
    case 0b00100: { // MOV Rd, #imm8
        uint8_t rd = (inst >> 8) & 7;
        uint32_t imm8 = inst & 0xFF;
        r[rd] = imm8;
        if (set_flags) set_nz(imm8);
        break;
    }
    case 0b00101: { // CMP Rn, #imm8
        uint8_t rn = (inst >> 8) & 7;
        bool cout, vout;
        uint32_t result = add_with_carry(r[rn], ~(uint32_t)(inst & 0xFF), true, cout, vout);
        set_nzcv(result, cout, vout);
        break;
    }
    case 0b00110: { // ADD Rn, #imm8
        uint8_t rn = (inst >> 8) & 7;
        bool cout, vout;
        uint32_t result = add_with_carry(r[rn], inst & 0xFF, false, cout, vout);
        r[rn] = result;
        if (set_flags) set_nzcv(result, cout, vout);
        break;
    }
    case 0b00111: { // SUB Rn, #imm8
        uint8_t rn = (inst >> 8) & 7;
        bool cout, vout;
        uint32_t result = add_with_carry(r[rn], ~(uint32_t)(inst & 0xFF), true, cout, vout);
        r[rn] = result;
        if (set_flags) set_nzcv(result, cout, vout);
        break;
    }
    // ---- Data processing / Special data / LDR literal ----
    case 0b01000: {
        if ((inst & 0xFC00) == 0x4000) {
            // Data processing register
            uint8_t rd = inst & 7, rm = (inst >> 3) & 7;
            uint8_t op = (inst >> 6) & 0xF;
            bool cout, vout;
            uint32_t result;
            switch (op) {
            case 0: // AND
                result = r[rd] & r[rm];
                r[rd] = result;
                if (set_flags) set_nz(result);
                break;
            case 1: // EOR
                result = r[rd] ^ r[rm];
                r[rd] = result;
                if (set_flags) set_nz(result);
                break;
            case 2: { // LSL
                uint8_t shift = r[rm] & 0xFF;
                bool carry;
                result = shift_c(r[rd], ShiftType::LSL, shift, carry);
                r[rd] = result;
                if (set_flags) set_nzc(result, shift ? carry : flag_c());
                break;
            }
            case 3: { // LSR
                uint8_t shift = r[rm] & 0xFF;
                bool carry;
                result = shift_c(r[rd], ShiftType::LSR, shift, carry);
                r[rd] = result;
                if (set_flags) set_nzc(result, shift ? carry : flag_c());
                break;
            }
            case 4: { // ASR
                uint8_t shift = r[rm] & 0xFF;
                bool carry;
                result = shift_c(r[rd], ShiftType::ASR, shift, carry);
                r[rd] = result;
                if (set_flags) set_nzc(result, shift ? carry : flag_c());
                break;
            }
            case 5: // ADC
                result = add_with_carry(r[rd], r[rm], flag_c(), cout, vout);
                r[rd] = result;
                if (set_flags) set_nzcv(result, cout, vout);
                break;
            case 6: // SBC
                result = add_with_carry(r[rd], ~r[rm], flag_c(), cout, vout);
                r[rd] = result;
                if (set_flags) set_nzcv(result, cout, vout);
                break;
            case 7: { // ROR
                uint8_t shift = r[rm] & 0xFF;
                bool carry;
                result = shift_c(r[rd], ShiftType::ROR, shift, carry);
                r[rd] = result;
                if (set_flags) set_nzc(result, shift ? carry : flag_c());
                break;
            }
            case 8: // TST
                result = r[rd] & r[rm];
                set_nz(result);
                break;
            case 9: // RSB (NEG)
                result = add_with_carry(~r[rm], 0, true, cout, vout);
                r[rd] = result;
                if (set_flags) set_nzcv(result, cout, vout);
                break;
            case 10: // CMP
                result = add_with_carry(r[rd], ~r[rm], true, cout, vout);
                set_nzcv(result, cout, vout);
                break;
            case 11: // CMN
                result = add_with_carry(r[rd], r[rm], false, cout, vout);
                set_nzcv(result, cout, vout);
                break;
            case 12: // ORR
                result = r[rd] | r[rm];
                r[rd] = result;
                if (set_flags) set_nz(result);
                break;
            case 13: // MUL
                result = r[rd] * r[rm];
                r[rd] = result;
                if (set_flags) set_nz(result);
                break;
            case 14: // BIC
                result = r[rd] & ~r[rm];
                r[rd] = result;
                if (set_flags) set_nz(result);
                break;
            case 15: // MVN
                result = ~r[rm];
                r[rd] = result;
                if (set_flags) set_nz(result);
                break;
            }
        } else if ((inst & 0xFF00) == 0x4400) {
            // ADD high registers
            uint8_t rd = ((inst >> 4) & 8) | (inst & 7);
            uint8_t rm = (inst >> 3) & 0xF;
            uint32_t val = read_reg(rd) + read_reg(rm);
            write_reg(rd, val);
        } else if ((inst & 0xFF00) == 0x4500) {
            // CMP high registers
            uint8_t rn = ((inst >> 4) & 8) | (inst & 7);
            uint8_t rm = (inst >> 3) & 0xF;
            bool cout, vout;
            uint32_t result = add_with_carry(read_reg(rn), ~read_reg(rm), true, cout, vout);
            set_nzcv(result, cout, vout);
        } else if ((inst & 0xFF00) == 0x4600) {
            // MOV high registers
            uint8_t rd = ((inst >> 4) & 8) | (inst & 7);
            uint8_t rm = (inst >> 3) & 0xF;
            write_reg(rd, read_reg(rm));
        } else if ((inst & 0xFF80) == 0x4700) {
            // BX Rm
            uint8_t rm = (inst >> 3) & 0xF;
            uint32_t target = read_reg(rm);
            // Check for exception return
            if ((target & 0xF0000000) == 0xF0000000) {
                exception_return(target);
            } else {
                r[15] = target & ~1u;
                branch_taken_ = true;
            }
        } else if ((inst & 0xFF80) == 0x4780) {
            // BLX Rm
            uint8_t rm = (inst >> 3) & 0xF;
            uint32_t target = read_reg(rm);
            r[14] = (r[15] + 2) | 1;
            r[15] = target & ~1u;
            branch_taken_ = true;
        }
        break;
    }
    // ---- LDR literal (PC-relative) ----
    case 0b01001: {
        uint8_t rt = (inst >> 8) & 7;
        uint32_t imm8 = (inst & 0xFF) << 2;
        uint32_t addr = ((r[15] + 4) & ~3u) + imm8;
        r[rt] = bus_->read32(addr);
        break;
    }
    // ---- Load/store register offset ----
    case 0b01010:
    case 0b01011: {
        uint8_t rt = inst & 7, rn = (inst >> 3) & 7, rm = (inst >> 6) & 7;
        uint32_t addr = r[rn] + r[rm];
        switch ((inst >> 9) & 7) {
        case 0: bus_->write32(addr, r[rt]); break;      // STR
        case 1: bus_->write16(addr, r[rt]); break;      // STRH
        case 2: bus_->write8(addr, r[rt]); break;       // STRB
        case 3: r[rt] = (int32_t)(int8_t)bus_->read8(addr); break;  // LDRSB
        case 4: r[rt] = bus_->read32(addr); break;      // LDR
        case 5: r[rt] = bus_->read16(addr); break;      // LDRH
        case 6: r[rt] = bus_->read8(addr); break;       // LDRB
        case 7: r[rt] = (int32_t)(int16_t)bus_->read16(addr); break; // LDRSH
        }
        break;
    }
    // ---- Load/store word/byte immediate offset ----
    case 0b01100: { // STR Rt, [Rn, #imm5*4]
        uint8_t rt = inst & 7, rn = (inst >> 3) & 7;
        uint32_t imm5 = ((inst >> 6) & 0x1F) << 2;
        bus_->write32(r[rn] + imm5, r[rt]);
        break;
    }
    case 0b01101: { // LDR Rt, [Rn, #imm5*4]
        uint8_t rt = inst & 7, rn = (inst >> 3) & 7;
        uint32_t imm5 = ((inst >> 6) & 0x1F) << 2;
        r[rt] = bus_->read32(r[rn] + imm5);
        break;
    }
    case 0b01110: { // STRB Rt, [Rn, #imm5]
        uint8_t rt = inst & 7, rn = (inst >> 3) & 7;
        uint32_t imm5 = (inst >> 6) & 0x1F;
        bus_->write8(r[rn] + imm5, r[rt]);
        break;
    }
    case 0b01111: { // LDRB Rt, [Rn, #imm5]
        uint8_t rt = inst & 7, rn = (inst >> 3) & 7;
        uint32_t imm5 = (inst >> 6) & 0x1F;
        r[rt] = bus_->read8(r[rn] + imm5);
        break;
    }
    // ---- Load/store halfword immediate offset ----
    case 0b10000: { // STRH Rt, [Rn, #imm5*2]
        uint8_t rt = inst & 7, rn = (inst >> 3) & 7;
        uint32_t imm5 = ((inst >> 6) & 0x1F) << 1;
        bus_->write16(r[rn] + imm5, r[rt]);
        break;
    }
    case 0b10001: { // LDRH Rt, [Rn, #imm5*2]
        uint8_t rt = inst & 7, rn = (inst >> 3) & 7;
        uint32_t imm5 = ((inst >> 6) & 0x1F) << 1;
        r[rt] = bus_->read16(r[rn] + imm5);
        break;
    }
    // ---- Load/store SP-relative ----
    case 0b10010: { // STR Rt, [SP, #imm8*4]
        uint8_t rt = (inst >> 8) & 7;
        uint32_t imm8 = (inst & 0xFF) << 2;
        bus_->write32(r[13] + imm8, r[rt]);
        break;
    }
    case 0b10011: { // LDR Rt, [SP, #imm8*4]
        uint8_t rt = (inst >> 8) & 7;
        uint32_t imm8 = (inst & 0xFF) << 2;
        r[rt] = bus_->read32(r[13] + imm8);
        break;
    }
    // ---- ADR / ADD SP ----
    case 0b10100: { // ADR Rd, label  (ADD Rd, PC, #imm8*4)
        uint8_t rd = (inst >> 8) & 7;
        uint32_t imm8 = (inst & 0xFF) << 2;
        r[rd] = ((r[15] + 4) & ~3u) + imm8;
        break;
    }
    case 0b10101: { // ADD Rd, SP, #imm8*4
        uint8_t rd = (inst >> 8) & 7;
        uint32_t imm8 = (inst & 0xFF) << 2;
        r[rd] = r[13] + imm8;
        break;
    }
    // ---- Miscellaneous ----
    case 0b10110:
    case 0b10111:
        exec_thumb16_misc(inst);
        break;
    // ---- STMIA / LDMIA ----
    case 0b11000: { // STMIA Rn!, {registers}
        uint8_t rn = (inst >> 8) & 7;
        uint32_t addr = r[rn];
        for (int i = 0; i < 8; i++)
            if (inst & (1 << i)) { bus_->write32(addr, r[i]); addr += 4; }
        r[rn] = addr;
        break;
    }
    case 0b11001: { // LDMIA Rn!, {registers}
        uint8_t rn = (inst >> 8) & 7;
        uint32_t addr = r[rn];
        for (int i = 0; i < 8; i++)
            if (inst & (1 << i)) { r[i] = bus_->read32(addr); addr += 4; }
        if (!(inst & (1 << rn))) r[rn] = addr; // Writeback if Rn not in list
        break;
    }
    // ---- Conditional branch / SVC ----
    case 0b11010:
    case 0b11011: {
        uint8_t cond = (inst >> 8) & 0xF;
        if (cond == 0xE) {
            // UDF (permanently undefined)
            fprintf(stderr, "[CPU] UDF at 0x%08X\n", r[15]);
            halted = true;
        } else if (cond == 0xF) {
            // SVC
            exception_entry(11); // SVCall = exception 11
        } else if (condition_passed(cond)) {
            int32_t offset = (int32_t)(int8_t)(inst & 0xFF) << 1;
            r[15] = r[15] + 4 + offset;
            branch_taken_ = true;
        }
        break;
    }
    // ---- Unconditional branch ----
    case 0b11100: {
        int32_t offset = inst & 0x7FF;
        if (offset & 0x400) offset |= 0xFFFFF800; // Sign-extend
        offset <<= 1;
        r[15] = r[15] + 4 + offset;
        branch_taken_ = true;
        break;
    }
    default:
        fprintf(stderr, "[CPU] Unknown Thumb-16 at 0x%08X: 0x%04X\n", r[15], inst);
        halted = true;
        break;
    }
}

// Misc 16-bit instructions (PUSH, POP, extend, hints, CPS, CBZ/CBNZ, IT)
void CPU::exec_thumb16_misc(uint16_t inst) {
    uint8_t op = (inst >> 8) & 0xF;

    switch (op) {
    case 0b0000: { // ADD SP, #imm7*4  or  SUB SP, #imm7*4
        uint32_t imm = (inst & 0x7F) << 2;
        if (inst & 0x80) set_sp(r[13] - imm);
        else set_sp(r[13] + imm);
        break;
    }
    case 0b0010: { // SXTH, SXTB, UXTH, UXTB
        uint8_t rd = inst & 7, rm = (inst >> 3) & 7;
        switch ((inst >> 6) & 3) {
        case 0: r[rd] = (int32_t)(int16_t)r[rm]; break; // SXTH
        case 1: r[rd] = (int32_t)(int8_t)r[rm]; break;  // SXTB
        case 2: r[rd] = r[rm] & 0xFFFF; break;          // UXTH
        case 3: r[rd] = r[rm] & 0xFF; break;            // UXTB
        }
        break;
    }
    case 0b0100: case 0b0101: { // PUSH {registers[, LR]}
        uint32_t addr = r[13];
        int count = __builtin_popcount(inst & 0xFF) + ((inst & 0x100) ? 1 : 0);
        addr -= count * 4;
        set_sp(addr);
        for (int i = 0; i < 8; i++)
            if (inst & (1 << i)) { bus_->write32(addr, r[i]); addr += 4; }
        if (inst & 0x100) bus_->write32(addr, r[14]); // LR
        break;
    }
    case 0b0110: { // CPS (CPSIE/CPSID)
        if (inst & 0x10) primask |= 1;   // CPSID i
        else { primask &= ~1u; irq_pending = true; } // CPSIE i
        break;
    }
    case 0b1010: { // REV, REV16, REVSH
        uint8_t rd = inst & 7, rm = (inst >> 3) & 7;
        uint32_t val = r[rm];
        switch ((inst >> 6) & 3) {
        case 0: // REV
            r[rd] = __builtin_bswap32(val);
            break;
        case 1: // REV16
            r[rd] = ((val & 0xFF00FF00) >> 8) | ((val & 0x00FF00FF) << 8);
            break;
        case 3: // REVSH
            r[rd] = (int32_t)(int16_t)__builtin_bswap16(val & 0xFFFF);
            break;
        }
        break;
    }
    case 0b1100: case 0b1101: { // POP {registers[, PC]}
        uint32_t addr = r[13];
        for (int i = 0; i < 8; i++)
            if (inst & (1 << i)) { r[i] = bus_->read32(addr); addr += 4; }
        if (inst & 0x100) {
            uint32_t val = bus_->read32(addr); addr += 4;
            // Check for exception return
            if ((val & 0xF0000000) == 0xF0000000) {
                set_sp(addr);
                exception_return(val);
                return;
            }
            r[15] = val & ~1u;
            branch_taken_ = true;
        }
        set_sp(addr);
        break;
    }
    case 0b1110: { // BKPT
        fprintf(stderr, "[CPU] BKPT #%u at 0x%08X (R0=%u)\n", inst & 0xFF, r[15], r[0]);
        halted = true;
        break;
    }
    case 0b1111: { // IT or hints
        uint8_t mask = inst & 0xF;
        if (mask == 0) {
            // Hints: NOP, YIELD, WFE, WFI, SEV
            switch ((inst >> 4) & 0xF) {
            case 0: break; // NOP
            case 1: break; // YIELD
            case 2: break; // WFE
            case 3: wfi = true; break; // WFI
            case 4: break; // SEV
            }
        } else {
            // IT instruction — set IT state
            uint8_t firstcond = (inst >> 4) & 0xF;
            set_it_state((firstcond << 4) | mask);
        }
        break;
    }
    default:
        // CBZ / CBNZ: 1011_x0x1
        if ((op & 0b0101) == 0b0001) {
            uint8_t rn = inst & 7;
            uint32_t imm = ((inst >> 2) & 0x3E) | ((inst & 0x200) >> 3);
            bool nonzero = inst & 0x0800;
            if ((r[rn] == 0) != nonzero) {
                r[15] = r[15] + 4 + imm;
                branch_taken_ = true;
            }
        } else {
            fprintf(stderr, "[CPU] Unknown misc Thumb-16 at 0x%08X: 0x%04X\n", r[15], inst);
        }
        break;
    }
}

// ============================================================================
// 32-bit Thumb execution
// ============================================================================

void CPU::exec_thumb32(uint16_t hw1, uint16_t hw2) {
    uint8_t op1 = (hw1 >> 11) & 3; // bits [12:11]
    uint8_t op2 = (hw1 >> 4) & 0x7F; // bits [10:4]

    switch (op1) {
    case 1: exec_thumb32_01(hw1, hw2, op2); break;
    case 2: exec_thumb32_10(hw1, hw2, op2); break;
    case 3: exec_thumb32_11(hw1, hw2, op2); break;
    default:
        fprintf(stderr, "[CPU] Invalid Thumb-32 op1=%d at 0x%08X\n", op1, r[15]);
        halted = true;
    }
}

// op1=01: Load/store multiple, load/store dual/exclusive, data processing (shifted register)
void CPU::exec_thumb32_01(uint16_t hw1, uint16_t hw2, uint8_t op2) {
    if ((op2 & 0x64) == 0x00) {
        // Load/store multiple: STMIA, LDMIA, STMDB, LDMDB
        exec_thumb32_ldst_multiple(hw1, hw2);
    } else if ((op2 & 0x64) == 0x04) {
        // Load/store dual, exclusive, table branch
        exec_thumb32_ldst_dual(hw1, hw2);
    } else if (!(op2 & 0x40)) {
        // Data processing (shifted register)
        exec_thumb32_dp_shifted_reg(hw1, hw2);
    } else {
        // Coprocessor / FP (op1=01, op2 bit 6 set)
        exec_vfp_ldst(hw1, hw2);
    }
}

// op1=10: Data processing (immediate), branches
void CPU::exec_thumb32_10(uint16_t hw1, uint16_t hw2, uint8_t /*op2*/) {
    if (hw2 & 0x8000) {
        // Branches and miscellaneous control
        exec_thumb32_branch(hw1, hw2);
    } else if (hw1 & 0x0200) {
        // Data processing (plain binary immediate)
        exec_thumb32_dp_plain_imm(hw1, hw2);
    } else {
        // Data processing (modified immediate)
        exec_thumb32_dp_mod_imm(hw1, hw2);
    }
}

// op1=11: Load/store single, data processing (register), multiply, long multiply
void CPU::exec_thumb32_11(uint16_t hw1, uint16_t hw2, uint8_t op2) {
    // op2 = hw1[10:4], dispatch based on ARM encoding:
    // op2[6:5]=00 (0x00-0x1F): Load/store single data items
    // op2[6:4]=010 (0x20-0x2F): Data processing (register): CLZ, REV, extends
    // op2[6:3]=0110 (0x30-0x37): Multiply, multiply accumulate
    // op2[6:3]=0111 (0x38-0x3F): Long multiply, divide
    // op2[6]=1 (0x40-0x7F): Coprocessor / FP
    if ((op2 & 0x60) == 0x00) {
        exec_thumb32_ldst_single(hw1, hw2);
    } else if ((op2 & 0x70) == 0x20) {
        exec_thumb32_dp_register(hw1, hw2);
    } else if ((op2 & 0x78) == 0x30) {
        exec_thumb32_multiply(hw1, hw2);
    } else if ((op2 & 0x78) == 0x38) {
        exec_thumb32_long_multiply(hw1, hw2);
    } else if (op2 & 0x40) {
        exec_vfp(hw1, hw2);
    } else {
        fprintf(stderr, "[CPU] Unimpl Thumb-32 op1=11 op2=0x%02X at 0x%08X\n", op2, r[15]);
    }
}

// ---- Load/Store Multiple ----
void CPU::exec_thumb32_ldst_multiple(uint16_t hw1, uint16_t hw2) {
    uint8_t rn = hw1 & 0xF;
    bool wback = hw1 & 0x20;
    bool load = hw1 & 0x10;
    bool decrement = hw1 & 0x100; // STMDB/LDMDB vs STMIA/LDMIA
    uint16_t reglist = hw2;

    int count = __builtin_popcount(reglist);
    uint32_t addr;

    if (decrement) {
        addr = read_reg(rn) - count * 4;
        if (wback) write_reg(rn, addr);
    } else {
        addr = read_reg(rn);
    }

    uint32_t base = addr;
    for (int i = 0; i < 16; i++) {
        if (!(reglist & (1 << i))) continue;
        if (load) {
            uint32_t val = bus_->read32(addr);
            if (i == 15) {
                r[15] = val & ~1u;
                branch_taken_ = true;
            } else {
                write_reg(i, val);
            }
        } else {
            bus_->write32(addr, read_reg(i));
        }
        addr += 4;
    }

    if (!decrement && wback)
        write_reg(rn, base + count * 4);
}

// ---- Load/Store Dual, Exclusive, Table Branch ----
void CPU::exec_thumb32_ldst_dual(uint16_t hw1, uint16_t hw2) {
    uint8_t op1 = (hw1 >> 7) & 3;
    uint8_t op2_bits = (hw1 >> 4) & 3;

    if (op1 == 0 && op2_bits == 0) {
        // STREX
        uint8_t rn = hw1 & 0xF;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t rd = (hw2 >> 8) & 0xF; // Status
        uint32_t imm8 = (hw2 & 0xFF) << 2;
        uint32_t addr = read_reg(rn) + imm8;
        bus_->write32(addr, read_reg(rt));
        r[rd] = 0; // Always succeed (no multicore)
        return;
    }
    if (op1 == 0 && op2_bits == 1) {
        // LDREX
        uint8_t rn = hw1 & 0xF;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint32_t imm8 = (hw2 & 0xFF) << 2;
        uint32_t addr = read_reg(rn) + imm8;
        r[rt] = bus_->read32(addr);
        return;
    }

    // TBB / TBH (must check before LDRD, as encoding overlaps)
    if ((hw1 & 0xFFF0) == 0xE8D0 && (hw2 & 0xFFE0) == 0xF000) {
        uint8_t rn = hw1 & 0xF;
        uint8_t rm = hw2 & 0xF;
        bool halfword = hw2 & 0x10;
        uint32_t base = read_reg(rn);
        uint32_t index = read_reg(rm);
        uint32_t offset;
        if (halfword)
            offset = bus_->read16(base + index * 2) * 2;
        else
            offset = bus_->read8(base + index) * 2;
        r[15] = r[15] + 4 + offset;
        branch_taken_ = true;
        return;
    }

    // STREXB, STREXH, LDREXB, LDREXH, CLREX
    if ((hw1 & 0xFFF0) == 0xE8D0) {
        uint8_t op3 = (hw2 >> 4) & 0xF;
        uint8_t rn = hw1 & 0xF;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t rd = hw2 & 0xF;
        uint32_t addr = read_reg(rn);
        switch (op3) {
        case 4: r[rt] = bus_->read8(addr); break;    // LDREXB
        case 5: r[rt] = bus_->read16(addr); break;   // LDREXH
        default: break;
        }
        (void)rd;
        return;
    }
    if ((hw1 & 0xFFF0) == 0xE8C0) {
        uint8_t op3 = (hw2 >> 4) & 0xF;
        uint8_t rn = hw1 & 0xF;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t rd = hw2 & 0xF;
        uint32_t addr = read_reg(rn);
        switch (op3) {
        case 4: bus_->write8(addr, read_reg(rt)); r[rd] = 0; break;  // STREXB
        case 5: bus_->write16(addr, read_reg(rt)); r[rd] = 0; break; // STREXH
        default: break;
        }
        return;
    }

    // LDRD / STRD (immediate)
    if ((hw1 & 0xFE50) == 0xE850 || (hw1 & 0xFE50) == 0xE840) {
        uint8_t rn = hw1 & 0xF;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t rt2 = (hw2 >> 8) & 0xF;
        uint32_t imm8 = (hw2 & 0xFF) << 2;
        bool P = hw1 & 0x100;
        bool U = hw1 & 0x80;
        bool W = hw1 & 0x20;
        bool L = hw1 & 0x10;

        uint32_t base = read_reg(rn);
        uint32_t offset_addr = U ? base + imm8 : base - imm8;
        uint32_t addr = P ? offset_addr : base;

        if (L) {
            r[rt] = bus_->read32(addr);
            r[rt2] = bus_->read32(addr + 4);
        } else {
            bus_->write32(addr, read_reg(rt));
            bus_->write32(addr + 4, read_reg(rt2));
        }

        if (W) write_reg(rn, offset_addr);
        return;
    }

    fprintf(stderr, "[CPU] Unimpl LDST dual/exc at 0x%08X: %04X %04X\n", r[15], hw1, hw2);
}

// ---- Data Processing (shifted register) ----
void CPU::exec_thumb32_dp_shifted_reg(uint16_t hw1, uint16_t hw2) {
    uint8_t op = (hw1 >> 5) & 0xF;
    uint8_t rn = hw1 & 0xF;
    bool S = hw1 & 0x10;
    uint8_t rd = (hw2 >> 8) & 0xF;
    uint8_t rm = hw2 & 0xF;

    // Decode shift
    uint8_t shift_type = (hw2 >> 4) & 3;
    uint8_t imm5 = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3);
    ShiftType st; uint8_t shift_n;
    decode_imm_shift(shift_type, imm5, st, shift_n);

    bool carry;
    uint32_t shifted = shift_c(read_reg(rm), st, shift_n, carry);
    bool cout, vout;
    uint32_t result;

    switch (op) {
    case 0x0: // AND / TST
        result = read_reg(rn) & shifted;
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x1: // BIC
        result = read_reg(rn) & ~shifted;
        write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x2: // ORR / MOV
        if (rn == 15) result = shifted; // MOV
        else result = read_reg(rn) | shifted;
        write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x3: // ORN / MVN
        if (rn == 15) result = ~shifted; // MVN
        else result = read_reg(rn) | ~shifted;
        write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x4: // EOR / TEQ
        result = read_reg(rn) ^ shifted;
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x8: // ADD / CMN
        result = add_with_carry(read_reg(rn), shifted, false, cout, vout);
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xA: // ADC
        result = add_with_carry(read_reg(rn), shifted, flag_c(), cout, vout);
        write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xB: // SBC
        result = add_with_carry(read_reg(rn), ~shifted, flag_c(), cout, vout);
        write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xD: // SUB / CMP
        result = add_with_carry(read_reg(rn), ~shifted, true, cout, vout);
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xE: // RSB
        result = add_with_carry(~read_reg(rn), shifted, true, cout, vout);
        write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    default:
        fprintf(stderr, "[CPU] Unimpl DP shifted reg op=%d at 0x%08X\n", op, r[15]);
        break;
    }
}

// ---- Data Processing (modified immediate) ----
void CPU::exec_thumb32_dp_mod_imm(uint16_t hw1, uint16_t hw2) {
    uint8_t op = (hw1 >> 5) & 0xF;
    uint8_t rn = hw1 & 0xF;
    bool S = hw1 & 0x10;
    uint8_t rd = (hw2 >> 8) & 0xF;

    uint16_t imm12 = ((hw1 & 0x400) << 1) | ((hw2 & 0x7000) >> 4) | (hw2 & 0xFF);
    bool carry;
    uint32_t imm32 = thumb_expand_imm_c(imm12, carry);
    bool cout, vout;
    uint32_t result;

    switch (op) {
    case 0x0: // AND / TST
        result = read_reg(rn) & imm32;
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x1: // BIC
        result = read_reg(rn) & ~imm32;
        write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x2: // ORR / MOV
        if (rn == 15) result = imm32;
        else result = read_reg(rn) | imm32;
        write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x3: // ORN / MVN
        if (rn == 15) result = ~imm32;
        else result = read_reg(rn) | ~imm32;
        write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x4: // EOR / TEQ
        result = read_reg(rn) ^ imm32;
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzc(result, carry);
        break;
    case 0x8: // ADD / CMN
        result = add_with_carry(read_reg(rn), imm32, false, cout, vout);
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xA: // ADC
        result = add_with_carry(read_reg(rn), imm32, flag_c(), cout, vout);
        write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xB: // SBC
        result = add_with_carry(read_reg(rn), ~imm32, flag_c(), cout, vout);
        write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xD: // SUB / CMP
        result = add_with_carry(read_reg(rn), ~imm32, true, cout, vout);
        if (rd != 15) write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    case 0xE: // RSB
        result = add_with_carry(~read_reg(rn), imm32, true, cout, vout);
        write_reg(rd, result);
        if (S) set_nzcv(result, cout, vout);
        break;
    default:
        fprintf(stderr, "[CPU] Unimpl DP mod imm op=%d at 0x%08X\n", op, r[15]);
        break;
    }
}

// ---- Data Processing (plain binary immediate): MOVW, MOVT, ADDW, SUBW, bitfield ----
void CPU::exec_thumb32_dp_plain_imm(uint16_t hw1, uint16_t hw2) {
    // Plain binary immediate: opcode at hw1[8:4] (5 bits)
    // NB: different from modified immediate which uses hw1[8:5] + S at hw1[4]
    uint8_t op = (hw1 >> 4) & 0x1F;
    uint8_t rn = hw1 & 0xF;
    uint8_t rd = (hw2 >> 8) & 0xF;

    // 16-bit immediate for MOVW/MOVT: imm4:i:imm3:imm8
    auto imm16 = [&]() -> uint32_t {
        return ((hw1 & 0xF) << 12) | ((hw1 & 0x400) << 1) | ((hw2 & 0x7000) >> 4) | (hw2 & 0xFF);
    };
    // 12-bit immediate for ADDW/SUBW
    auto imm12 = [&]() -> uint32_t {
        return ((hw1 & 0x400) << 1) | ((hw2 & 0x7000) >> 4) | (hw2 & 0xFF);
    };

    switch (op) {
    case 0x00: // ADDW Rd, Rn, #imm12  (or ADR if Rn=15)
        if (rn == 15) r[rd] = ((r[15] + 4) & ~3u) + imm12();
        else r[rd] = read_reg(rn) + imm12();
        break;
    case 0x04: // MOVW Rd, #imm16
        r[rd] = imm16();
        break;
    case 0x0A: // SUBW Rd, Rn, #imm12  (or ADR if Rn=15)
        if (rn == 15) r[rd] = ((r[15] + 4) & ~3u) - imm12();
        else r[rd] = read_reg(rn) - imm12();
        break;
    case 0x0C: // MOVT Rd, #imm16
        r[rd] = (r[rd] & 0xFFFF) | (imm16() << 16);
        break;
    case 0x10: case 0x12: { // SSAT
        uint8_t sat_bits = (hw2 & 0x1F) + 1;
        uint8_t shift_type = (hw2 >> 4) & 3;
        uint8_t imm5 = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3);
        ShiftType st; uint8_t shift_n;
        decode_imm_shift(shift_type & 2 ? 2 : 0, imm5, st, shift_n);
        bool c;
        int32_t val = (int32_t)shift_c(read_reg(rn), st, shift_n, c);
        int32_t max = (1 << (sat_bits - 1)) - 1;
        int32_t min = -(1 << (sat_bits - 1));
        if (val > max) { r[rd] = max; xpsr |= (1u << 27); }
        else if (val < min) { r[rd] = min; xpsr |= (1u << 27); }
        else r[rd] = val;
        break;
    }
    case 0x14: { // SBFX
        uint8_t lsb = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3);
        uint8_t width = (hw2 & 0x1F) + 1;
        int32_t val = (int32_t)(read_reg(rn) << (32 - lsb - width)) >> (32 - width);
        r[rd] = val;
        break;
    }
    case 0x18: case 0x1A: { // USAT
        uint8_t sat_bits = hw2 & 0x1F;
        uint8_t shift_type = (hw2 >> 4) & 3;
        uint8_t imm5 = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3);
        ShiftType st; uint8_t shift_n;
        decode_imm_shift(shift_type & 2 ? 2 : 0, imm5, st, shift_n);
        bool c;
        int32_t val = (int32_t)shift_c(read_reg(rn), st, shift_n, c);
        uint32_t max = (1u << sat_bits) - 1;
        if (val < 0) { r[rd] = 0; xpsr |= (1u << 27); }
        else if ((uint32_t)val > max) { r[rd] = max; xpsr |= (1u << 27); }
        else r[rd] = val;
        break;
    }
    case 0x16: { // BFC / BFI
        uint8_t lsb = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3);
        uint8_t msb = hw2 & 0x1F;
        uint8_t width = msb - lsb + 1;
        uint32_t mask = ((1u << width) - 1) << lsb;
        if (rn == 15) {
            r[rd] &= ~mask; // BFC
        } else {
            r[rd] = (r[rd] & ~mask) | ((read_reg(rn) << lsb) & mask); // BFI
        }
        break;
    }
    case 0x1C: { // UBFX
        uint8_t lsb = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3);
        uint8_t width = (hw2 & 0x1F) + 1;
        r[rd] = (read_reg(rn) >> lsb) & ((1u << width) - 1);
        break;
    }
    default:
        fprintf(stderr, "[CPU] Unimpl DP plain imm op=%d at 0x%08X\n", op, r[15]);
        break;
    }
}

// ---- Branches and miscellaneous control ----
void CPU::exec_thumb32_branch(uint16_t hw1, uint16_t hw2) {
    uint8_t op = (hw2 >> 12) & 7;

    if ((op & 5) == 0) {
        // Conditional branch or misc control
        if ((hw1 & 0x0380) != 0x0380) {
            // Conditional B (T3)
            uint8_t cond = (hw1 >> 6) & 0xF;
            if (condition_passed(cond)) {
                uint32_t S = (hw1 >> 10) & 1;
                uint32_t J2 = (hw2 >> 11) & 1;
                uint32_t J1 = (hw2 >> 13) & 1;
                uint32_t imm6 = hw1 & 0x3F;
                uint32_t imm11 = hw2 & 0x7FF;
                int32_t offset = (S << 20) | (J2 << 19) | (J1 << 18) | (imm6 << 12) | (imm11 << 1);
                if (S) offset |= 0xFFE00000; // Sign-extend
                r[15] = r[15] + 4 + offset;
                branch_taken_ = true;
            }
        } else {
            // Miscellaneous control
            exec_thumb32_misc_control(hw1, hw2);
        }
        return;
    }

    if ((op & 5) == 1) {
        // B.W (T4)
        uint32_t S = (hw1 >> 10) & 1;
        uint32_t J1 = (hw2 >> 13) & 1;
        uint32_t J2 = (hw2 >> 11) & 1;
        uint32_t I1 = !(J1 ^ S);
        uint32_t I2 = !(J2 ^ S);
        uint32_t imm10 = hw1 & 0x3FF;
        uint32_t imm11 = hw2 & 0x7FF;
        int32_t offset = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
        if (S) offset |= 0xFE000000; // Sign-extend
        r[15] = r[15] + 4 + offset;
        branch_taken_ = true;
        return;
    }

    if ((op & 5) == 5) {
        // BL (T1)
        uint32_t S = (hw1 >> 10) & 1;
        uint32_t J1 = (hw2 >> 13) & 1;
        uint32_t J2 = (hw2 >> 11) & 1;
        uint32_t I1 = !(J1 ^ S);
        uint32_t I2 = !(J2 ^ S);
        uint32_t imm10 = hw1 & 0x3FF;
        uint32_t imm11 = hw2 & 0x7FF;
        int32_t offset = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
        if (S) offset |= 0xFE000000;
        r[14] = (r[15] + 4) | 1;
        r[15] = r[15] + 4 + offset;
        branch_taken_ = true;
        return;
    }

    fprintf(stderr, "[CPU] Unimpl branch op=%d at 0x%08X\n", op, r[15]);
}

// ---- Miscellaneous control (MSR, MRS, DSB, DMB, ISB, etc.) ----
void CPU::exec_thumb32_misc_control(uint16_t hw1, uint16_t hw2) {
    // Dispatch on hw1[7:5]: 100=MSR, 101=Hints/Barriers, 111=MRS
    uint8_t cat = (hw1 >> 5) & 7;

    if (cat == 4) {
        // MSR (register): hw1[7:5] = 100
        uint8_t rn = hw1 & 0xF;
        uint8_t sysm = hw2 & 0xFF;
        uint32_t val = read_reg(rn);
        switch (sysm) {
        case 0: // APSR (flags only)
            xpsr = (xpsr & 0x07FFFFFF) | (val & 0xF8000000);
            break;
        case 1: case 2: case 3: // IAPSR, EAPSR, PSR
            xpsr = (xpsr & 0x07FFFFFF) | (val & 0xF8000000);
            break;
        case 16: primask = val & 1; irq_pending = true; break;
        case 17: basepri = val & 0xFF; irq_pending = true; break;
        case 18: // BASEPRI_MAX
            if ((val & 0xFF) != 0 && ((val & 0xFF) < basepri || basepri == 0))
                basepri = val & 0xFF;
            irq_pending = true;
            break;
        case 8:  // MSP
            msp = val & ~3u;
            if (in_handler_mode() || !(control & 2)) r[13] = msp;
            break;
        case 9:  // PSP
            psp = val & ~3u;
            if (!in_handler_mode() && (control & 2)) r[13] = psp;
            break;
        case 19: faultmask = val & 1; irq_pending = true; break;
        case 20: {
            bool was_psp = !in_handler_mode() && (control & 2);
            control = val & 3;
            bool now_psp = !in_handler_mode() && (control & 2);
            if (was_psp != now_psp) r[13] = now_psp ? psp : msp;
            break;
        }
        }
        return;
    }

    if (cat == 7) {
        // MRS: hw1[7:5] = 111
        uint8_t rd = (hw2 >> 8) & 0xF;
        uint8_t sysm = hw2 & 0xFF;
        switch (sysm) {
        case 0: case 1: case 2: case 3:
            r[rd] = xpsr; break;
        case 5: case 6: case 7:
            r[rd] = xpsr & 0x1FF; break; // IPSR (9 bits)
        case 8: r[rd] = msp; break;     // MSP
        case 9: r[rd] = psp; break;     // PSP
        case 16: r[rd] = primask; break;
        case 17: r[rd] = basepri; break;
        case 19: r[rd] = faultmask; break;
        case 20: r[rd] = control; break;
        }
        return;
    }

    // Hints and barriers: hw1[7:5] = 101 (or other)
    // DSB, DMB, ISB are NOPs for single-core emulation
    return;

    fprintf(stderr, "[CPU] Unimpl misc control at 0x%08X: %04X %04X\n", r[15], hw1, hw2);
}

// ---- Load/Store Single ----
void CPU::exec_thumb32_ldst_single(uint16_t hw1, uint16_t hw2) {
    uint8_t size = (hw1 >> 5) & 3;  // 00=byte, 01=half, 10=word
    bool sign = hw1 & 0x800;        // hw1[11]: signed load
    bool load = hw1 & 0x10;         // hw1[4]: load vs store
    bool imm12_form = hw1 & 0x80;   // hw1[7]: 1=positive imm12, 0=imm8/register
    uint8_t rn = hw1 & 0xF;
    uint8_t rt = (hw2 >> 12) & 0xF;

    uint32_t base = (rn == 15) ? ((r[15] + 4) & ~3u) : read_reg(rn);
    uint32_t addr;

    if (rn == 15) {
        // PC-relative (literal)
        uint32_t imm = imm12_form ? (hw2 & 0xFFF) : (hw2 & 0xFFF);
        addr = imm12_form ? base + imm : base - imm;
    } else if (imm12_form) {
        // Positive immediate 12-bit offset
        addr = base + (hw2 & 0xFFF);
    } else if (hw2 & 0x800) {
        // Pre/post-indexed with 8-bit immediate
        uint32_t imm8 = hw2 & 0xFF;
        bool P = hw2 & 0x400;
        bool U = hw2 & 0x200;
        bool W = hw2 & 0x100;
        uint32_t offset = U ? imm8 : -imm8;
        uint32_t offset_addr = base + offset;
        addr = P ? offset_addr : base;
        if (W) write_reg(rn, offset_addr);
    } else {
        // Register offset with shift
        uint8_t rm = hw2 & 0xF;
        uint8_t shift = (hw2 >> 4) & 3;
        addr = base + (read_reg(rm) << shift);
    }

    if (load) {
        uint32_t val;
        switch (size) {
        case 0: // Byte
            val = bus_->read8(addr);
            if (sign) val = (int32_t)(int8_t)val;
            break;
        case 1: // Half
            val = bus_->read16(addr);
            if (sign) val = (int32_t)(int16_t)val;
            break;
        case 2: // Word
            val = bus_->read32(addr);
            break;
        default: val = 0;
        }
        if (rt == 15) {
            r[15] = val & ~1u;
            branch_taken_ = true;
        } else {
            r[rt] = val;
        }
    } else {
        switch (size) {
        case 0: bus_->write8(addr, read_reg(rt)); break;
        case 1: bus_->write16(addr, read_reg(rt)); break;
        case 2: bus_->write32(addr, read_reg(rt)); break;
        }
    }
}

// ---- Data Processing (register): CLZ, RBIT, REV, UXTB, SXTH, etc. ----
void CPU::exec_thumb32_dp_register(uint16_t hw1, uint16_t hw2) {
    uint8_t op1 = (hw1 >> 4) & 0xF;
    uint8_t op2 = (hw2 >> 4) & 0xF;
    uint8_t rn = hw1 & 0xF;
    uint8_t rd = (hw2 >> 8) & 0xF;
    uint8_t rm = hw2 & 0xF;

    if ((op1 & 0x8) == 0 && op2 == 0) {
        // LSL/LSR/ASR/ROR (register) wide — op1[2:1]=shift type, op1[0]=S
        uint8_t op = (hw1 >> 5) & 3;
        ShiftType st = (ShiftType)op;
        uint8_t shift = read_reg(rm) & 0xFF;
        bool carry;
        uint32_t result = shift_c(read_reg(rn), st, shift, carry);
        r[rd] = result;
        if (hw1 & 0x10) set_nzc(result, shift ? carry : flag_c());
        return;
    }

    if (op1 <= 5 && op2 != 0) {
        // Sign/zero extend (SXTH, SXTB, UXTH, UXTB, SXTB16, etc.)
        uint8_t rotation = ((hw2 >> 4) & 3) * 8;
        uint32_t val = read_reg(rm);
        if (rotation) val = (val >> rotation) | (val << (32 - rotation));

        switch (hw1 & 0xF0) {
        case 0x00: // SXTH / SXTAH
            if (rn == 15) r[rd] = (int32_t)(int16_t)val;
            else r[rd] = read_reg(rn) + (int32_t)(int16_t)val;
            break;
        case 0x10: // UXTAH / UXTH
            if (rn == 15) r[rd] = val & 0xFFFF;
            else r[rd] = read_reg(rn) + (val & 0xFFFF);
            break;
        case 0x40: // SXTB / SXTAB
            if (rn == 15) r[rd] = (int32_t)(int8_t)val;
            else r[rd] = read_reg(rn) + (int32_t)(int8_t)val;
            break;
        case 0x50: // UXTB / UXTAB
            if (rn == 15) r[rd] = val & 0xFF;
            else r[rd] = read_reg(rn) + (val & 0xFF);
            break;
        default:
            fprintf(stderr, "[CPU] Unimpl extend at 0x%08X\n", r[15]);
            break;
        }
        return;
    }

    if (op1 == 1 && op2 == 0) {
        // Parallel add/sub — not common, skip for now
        fprintf(stderr, "[CPU] Unimpl parallel add/sub at 0x%08X\n", r[15]);
        return;
    }

    // Misc operations (op1=8-11: REV, REV16, RBIT, REVSH, CLZ)
    if ((op1 & 0xC) == 0x8) {
        // RBIT, REV, REV16, REVSH, CLZ
        uint32_t val = read_reg(rm);
        switch (op2) {
        case 0x0: // REV (also check op1)
            if (op1 == 2) r[rd] = __builtin_bswap32(val);
            break;
        case 0x1: // REV16
            r[rd] = ((val & 0xFF00FF00) >> 8) | ((val & 0x00FF00FF) << 8);
            break;
        case 0x2: // RBIT
            {
                uint32_t result = 0;
                for (int i = 0; i < 32; i++)
                    if (val & (1u << i)) result |= (1u << (31 - i));
                r[rd] = result;
            }
            break;
        case 0x3: // REVSH
            r[rd] = (int32_t)(int16_t)__builtin_bswap16(val & 0xFFFF);
            break;
        case 0x8: // CLZ
            r[rd] = val ? __builtin_clz(val) : 32;
            break;
        default:
            fprintf(stderr, "[CPU] Unimpl misc DP reg op2=%d at 0x%08X\n", op2, r[15]);
            break;
        }
        return;
    }

    fprintf(stderr, "[CPU] Unimpl DP register op1=%d op2=%d at 0x%08X\n", op1, op2, r[15]);
}

// ---- Multiply and multiply accumulate ----
void CPU::exec_thumb32_multiply(uint16_t hw1, uint16_t hw2) {
    uint8_t op1 = (hw1 >> 4) & 7;
    uint8_t op2 = (hw2 >> 4) & 3;
    uint8_t rn = hw1 & 0xF;
    uint8_t rd = (hw2 >> 8) & 0xF;
    uint8_t rm = hw2 & 0xF;
    uint8_t ra = (hw2 >> 12) & 0xF;

    switch (op1) {
    case 0:
        if (ra == 15) // MUL
            r[rd] = read_reg(rn) * read_reg(rm);
        else if (op2 == 0) // MLA
            r[rd] = read_reg(rn) * read_reg(rm) + read_reg(ra);
        else // MLS
            r[rd] = read_reg(ra) - read_reg(rn) * read_reg(rm);
        break;
    default:
        fprintf(stderr, "[CPU] Unimpl multiply op1=%d at 0x%08X\n", op1, r[15]);
        break;
    }
}

// ---- Long multiply, long multiply accumulate, divide ----
void CPU::exec_thumb32_long_multiply(uint16_t hw1, uint16_t hw2) {
    uint8_t op1 = (hw1 >> 4) & 7;
    uint8_t op2 = (hw2 >> 4) & 0xF;
    uint8_t rn = hw1 & 0xF;
    uint8_t rdlo = (hw2 >> 12) & 0xF;
    uint8_t rdhi = (hw2 >> 8) & 0xF;
    uint8_t rm = hw2 & 0xF;

    switch (op1) {
    case 0: { // SMULL
        int64_t result = (int64_t)(int32_t)read_reg(rn) * (int32_t)read_reg(rm);
        r[rdlo] = (uint32_t)result;
        r[rdhi] = (uint32_t)(result >> 32);
        break;
    }
    case 1: // SDIV
        if (op2 == 0xF) {
            int32_t n = (int32_t)read_reg(rn);
            int32_t m = (int32_t)read_reg(rm);
            r[rdhi] = m ? (uint32_t)(n / m) : 0; // rdhi is actually rd for DIV
        }
        break;
    case 2: { // UMULL
        uint64_t result = (uint64_t)read_reg(rn) * read_reg(rm);
        r[rdlo] = (uint32_t)result;
        r[rdhi] = (uint32_t)(result >> 32);
        break;
    }
    case 3: // UDIV
        if (op2 == 0xF) {
            uint32_t n = read_reg(rn);
            uint32_t m = read_reg(rm);
            r[rdhi] = m ? n / m : 0;
        }
        break;
    case 4: { // SMLAL
        int64_t acc = ((int64_t)(int32_t)r[rdhi] << 32) | r[rdlo];
        acc += (int64_t)(int32_t)read_reg(rn) * (int32_t)read_reg(rm);
        r[rdlo] = (uint32_t)acc;
        r[rdhi] = (uint32_t)(acc >> 32);
        break;
    }
    case 6: { // UMLAL
        uint64_t acc = ((uint64_t)r[rdhi] << 32) | r[rdlo];
        acc += (uint64_t)read_reg(rn) * read_reg(rm);
        r[rdlo] = (uint32_t)acc;
        r[rdhi] = (uint32_t)(acc >> 32);
        break;
    }
    default:
        fprintf(stderr, "[CPU] Unimpl long mul/div op1=%d at 0x%08X\n", op1, r[15]);
        break;
    }
}

// ============================================================================
// VFP (Floating-Point) Instructions — VFPv4-D16
// ============================================================================

// VFP data processing (CDP/CDP2, MCR/MRC) — op1=11, op2 bit 6 set
void CPU::exec_vfp(uint16_t hw1, uint16_t hw2) {
    uint8_t opc1 = (hw1 >> 4) & 0xF;
    uint8_t cp = (hw2 >> 8) & 0xF;

    // VMOV between ARM core and single-precision (MCR/MRC, cp10)
    if (cp == 10 && (opc1 & 0xE) == 0xE) {
        uint8_t rt = (hw1 & 0xF);
        // Re-decode: this is VMRS/VMSR or VMOV
        // MRC: bit 20 set (read from coproc to ARM)
        // MCR: bit 20 clear (write from ARM to coproc)
        bool to_arm = hw1 & 0x10; // L bit for MRC
        uint8_t crn = (hw2 >> 16) & 0xF;

        if (opc1 == 0xF && to_arm) {
            // VMRS: Move FP status to ARM register
            if (crn == 1) { // FPSCR
                if (rt == 15) {
                    // VMRS APSR_nzcv, FPSCR — copy N,Z,C,V from FPSCR to xPSR
                    xpsr = (xpsr & 0x0FFFFFFF) | (fpscr & 0xF0000000);
                } else {
                    r[rt] = fpscr;
                }
            }
            return;
        }
        if (opc1 == 0xE && !to_arm) {
            // VMSR: Move ARM register to FP status
            if (crn == 1) fpscr = r[rt];
            return;
        }
    }

    // Properly decode VFP data processing and VMOV instructions
    // Re-parse the full 32-bit instruction
    uint32_t inst = (hw1 << 16) | hw2;

    // VMOV between ARM core register and single-precision
    if ((inst & 0x0F100F10) == 0x0E000A10) {
        bool to_arm = inst & (1<<20);
        uint8_t rt2 = (inst >> 12) & 0xF;
        uint8_t vn2 = ((inst >> 16) & 0xF) | ((inst & (1<<7)) ? 16 : 0);
        if (to_arm) r[rt2] = s[vn2];
        else s[vn2] = r[rt2];
        return;
    }

    // VFP CDP (data processing)
    if ((inst & 0x0F000E10) == 0x0E000A00) {
        exec_vfp_dp(inst);
        return;
    }

    fprintf(stderr, "[CPU] Unimpl VFP hw1=%04X hw2=%04X at 0x%08X\n", hw1, hw2, r[15]);
}

// VFP load/store and 64-bit transfers (op1=01, coprocessor)
void CPU::exec_vfp_ldst(uint16_t hw1, uint16_t hw2) {
    uint32_t inst = (hw1 << 16) | hw2;
    uint8_t cp = (hw2 >> 8) & 0xF;

    if (cp != 10 && cp != 11) {
        // Not FP coprocessor
        fprintf(stderr, "[CPU] Coprocessor cp%d hw1=%04X hw2=%04X at 0x%08X\n",
                cp, hw1, hw2, r[15]);
        return;
    }

    bool is_dp = (cp == 11);
    uint8_t p = (inst >> 24) & 1;
    uint8_t u = (inst >> 23) & 1;
    uint8_t w = (inst >> 21) & 1;
    uint8_t l = (inst >> 20) & 1;
    uint8_t rn = (inst >> 16) & 0xF;
    uint8_t imm8 = inst & 0xFF;
    uint32_t offset = imm8 * 4;

    if (is_dp) {
        uint8_t vd = ((inst >> 12) & 0xF) | ((inst & (1<<22)) ? 16 : 0);
        vd /= 2; // For double, Dd index. Actually: D:Vd = (inst[22]<<4)|(inst[15:12])
        // Re-decode properly for double
        uint8_t d = (inst >> 22) & 1;
        uint8_t vd_raw = (inst >> 12) & 0xF;
        vd = (d << 4) | vd_raw;
        // vd is now the Dd register number

        if (p == 1 && w == 0) {
            // VLDR/VSTR (immediate, no writeback)
            uint32_t base = (rn == 15) ? ((r[15] + 4) & ~3u) : r[rn];
            uint32_t addr = u ? base + offset : base - offset;
            if (l) { // VLDR
                s[vd*2]   = bus_->read32(addr);
                s[vd*2+1] = bus_->read32(addr + 4);
            } else { // VSTR
                bus_->write32(addr, s[vd*2]);
                bus_->write32(addr + 4, s[vd*2+1]);
            }
            return;
        }
        if (p == 0 || w == 1) {
            // VLDM/VSTM (multiple)
            uint32_t base = r[rn];
            if (!u) base -= offset; // Decrement before
            uint32_t addr = u ? base : base;
            uint8_t regs = imm8 / 2;
            for (int i = 0; i < regs && (vd + i) < 16; i++) {
                if (l) {
                    s[(vd+i)*2]   = bus_->read32(addr);
                    s[(vd+i)*2+1] = bus_->read32(addr + 4);
                } else {
                    bus_->write32(addr, s[(vd+i)*2]);
                    bus_->write32(addr + 4, s[(vd+i)*2+1]);
                }
                addr += 8;
            }
            if (w) {
                if (u) r[rn] = base + offset;
                else r[rn] = base - offset;
                if (rn == 13) { if (control & 2) psp = r[13]; else msp = r[13]; }
            }
            return;
        }
    } else {
        // Single-precision
        uint8_t d = (inst >> 22) & 1;
        uint8_t vd_raw = (inst >> 12) & 0xF;
        uint8_t vd = (vd_raw << 1) | d;

        if (p == 1 && w == 0) {
            // VLDR/VSTR single
            uint32_t base = (rn == 15) ? ((r[15] + 4) & ~3u) : r[rn];
            uint32_t addr = u ? base + offset : base - offset;
            if (l) s[vd] = bus_->read32(addr);
            else bus_->write32(addr, s[vd]);
            return;
        }
        if (p == 0 || w == 1) {
            // VLDM/VSTM single
            uint32_t base = r[rn];
            if (!u) base -= offset;
            uint32_t addr = base;
            for (int i = 0; i < imm8 && (vd + i) < 32; i++) {
                if (l) s[vd+i] = bus_->read32(addr);
                else bus_->write32(addr, s[vd+i]);
                addr += 4;
            }
            if (w) {
                if (u) r[rn] = base + offset;
                else r[rn] = base;
                if (rn == 13) { if (control & 2) psp = r[13]; else msp = r[13]; }
            }
            return;
        }
    }

    // VMOV between two ARM core regs and two single-precision / one double
    if (cp == 10 && p == 0 && !w) {
        uint8_t rt = (inst >> 12) & 0xF;
        uint8_t rt2 = (inst >> 16) & 0xF;
        uint8_t vm = ((inst & 0xF) << 1) | ((inst >> 5) & 1);
        if (l) { r[rt] = s[vm]; r[rt2] = s[vm+1]; }
        else   { s[vm] = r[rt]; s[vm+1] = r[rt2]; }
        return;
    }
    if (cp == 11 && p == 0 && !w) {
        uint8_t rt = (inst >> 12) & 0xF;
        uint8_t rt2 = (inst >> 16) & 0xF;
        uint8_t vm = ((inst >> 5) & 1) << 4 | (inst & 0xF);
        if (l) { r[rt] = s[vm*2]; r[rt2] = s[vm*2+1]; }
        else   { s[vm*2] = r[rt]; s[vm*2+1] = r[rt2]; }
        return;
    }

    fprintf(stderr, "[CPU] Unimpl VFP ldst hw1=%04X hw2=%04X at 0x%08X\n", hw1, hw2, r[15]);
}

// VFP data processing (single-precision operations)
void CPU::exec_vfp_dp(uint32_t inst) {
    bool is_dp = inst & 0x100;
    uint8_t opc1 = (inst >> 20) & 0xF;
    uint8_t opc3 = (inst >> 6) & 0x3;

    if (!is_dp) {
        // Single-precision
        uint8_t d = (inst >> 22) & 1;
        uint8_t vd = (((inst >> 12) & 0xF) << 1) | d;
        uint8_t n = (inst >> 7) & 1;
        uint8_t vn = (((inst >> 16) & 0xF) << 1) | n;
        uint8_t m = (inst >> 5) & 1;
        uint8_t vm = ((inst & 0xF) << 1) | m;

        float a = sf(vn), b = sf(vm);
        float result;
        switch (opc1 & 0xB) { // Remove bit 2 (opc1[2] is part of some encodings)
        case 0x0: // VMLA/VMLS
            result = (opc1 & 4) ? sf(vd) - a*b : sf(vd) + a*b;
            sf(vd) = result; return;
        case 0x1: // VNMLA/VNMLS/VNMUL
            if (opc3 & 1) { sf(vd) = -(a*b); return; } // VNMUL
            result = (opc1 & 4) ? -(sf(vd) - a*b) : -(sf(vd) + a*b);
            sf(vd) = result; return;
        case 0x2: // VMUL/VNMUL
            sf(vd) = (opc1 & 4) ? a*b : a*b; // bit 2 selects VMUL vs VNMUL
            return;
        case 0x3: // VADD/VSUB
            sf(vd) = (opc1 & 4) ? a - b : a + b; return;
        case 0x8: // VDIV
            sf(vd) = a / b; return;
        case 0xB: // Misc: VMOV, VABS, VNEG, VSQRT, VCMP, VCVT
            exec_vfp_misc_single(inst, vd, vm);
            return;
        }
    } else {
        // Double-precision
        uint8_t d = (inst >> 22) & 1;
        uint8_t vd = (d << 4) | ((inst >> 12) & 0xF);
        uint8_t n = (inst >> 7) & 1;
        uint8_t vn = (n << 4) | ((inst >> 16) & 0xF);
        uint8_t m = (inst >> 5) & 1;
        uint8_t vm_idx = (m << 4) | (inst & 0xF);

        double a = df(vn), b = df(vm_idx);
        switch (opc1 & 0xB) {
        case 0x0: set_df(vd, (opc1&4) ? df(vd)-a*b : df(vd)+a*b); return;
        case 0x2: set_df(vd, a*b); return;
        case 0x3: set_df(vd, (opc1&4) ? a-b : a+b); return;
        case 0x8: set_df(vd, a/b); return;
        case 0xB: exec_vfp_misc_double(inst, vd, vm_idx); return;
        }
    }
    fprintf(stderr, "[CPU] Unimpl VFP dp opc1=%d at 0x%08X\n", opc1, r[15]);
}

void CPU::exec_vfp_misc_single(uint32_t inst, uint8_t vd, uint8_t vm) {
    uint8_t opc2 = (inst >> 16) & 0xF;
    uint8_t opc3 = (inst >> 6) & 0x3;

    switch (opc2) {
    case 0x0: // VMOV imm, VMOV reg, VABS
        if (opc3 & 1) sf(vd) = fabsf(sf(vm)); // VABS
        else { s[vd] = s[vm]; } // VMOV reg
        return;
    case 0x1: // VNEG, VSQRT
        if (opc3 & 1) sf(vd) = sqrtf(sf(vm)); // VSQRT
        else sf(vd) = -sf(vm); // VNEG
        return;
    case 0x4: case 0x5: // VCMP, VCMPE
    {
        float a = sf(vd);
        float b = (opc2 & 1) ? 0.0f : sf(vm);
        uint32_t nzcv = 0;
        if (a == b) nzcv = 0x6000; // Z=1, C=1
        else if (a < b) nzcv = 0x8000; // N=1
        else nzcv = 0x2000; // C=1
        fpscr = (fpscr & 0x0FFFFFFF) | (nzcv << 16);
        return;
    }
    case 0x7: // VCVT between double and single
        if (opc3 & 1) {
            // VCVT.F64.F32 Dd, Sm → promote single to double
            set_df(vd / 2, (double)sf(vm));
        }
        return;
    case 0x8: // VCVT.F32.S32/U32 (int to float)
        if (opc3 & 1) sf(vd) = (float)(int32_t)s[vm];  // Signed
        else sf(vd) = (float)s[vm];                     // Unsigned
        return;
    case 0xC: case 0xD: // VCVT.U32.F32 / VCVT.S32.F32 (float to int)
    {
        float v = sf(vm);
        if (opc2 & 1) s[vd] = (int32_t)v;   // Signed (round toward zero for VCVT)
        else s[vd] = (uint32_t)v;            // Unsigned
        return;
    }
    }
    fprintf(stderr, "[CPU] Unimpl VFP misc single opc2=%d opc3=%d at 0x%08X\n",
            opc2, opc3, r[15]);
}

void CPU::exec_vfp_misc_double(uint32_t inst, uint8_t vd, uint8_t vm) {
    uint8_t opc2 = (inst >> 16) & 0xF;
    uint8_t opc3 = (inst >> 6) & 0x3;

    switch (opc2) {
    case 0x0:
        if (opc3 & 1) set_df(vd, fabs(df(vm)));
        else set_df(vd, df(vm)); // VMOV
        return;
    case 0x1:
        if (opc3 & 1) set_df(vd, sqrt(df(vm)));
        else set_df(vd, -df(vm));
        return;
    case 0x4: case 0x5: {
        double a = df(vd);
        double b = (opc2 & 1) ? 0.0 : df(vm);
        uint32_t nzcv = 0;
        if (a == b) nzcv = 0x6000;
        else if (a < b) nzcv = 0x8000;
        else nzcv = 0x2000;
        fpscr = (fpscr & 0x0FFFFFFF) | (nzcv << 16);
        return;
    }
    case 0x7: // VCVT.F32.F64 Sd, Dm → demote double to single
        if (!(opc3 & 1)) {
            sf(vd) = (float)df(vm);
        }
        return;
    case 0x8:
        if (opc3 & 1) set_df(vd, (double)(int32_t)s[vm*2]);
        else set_df(vd, (double)s[vm*2]);
        return;
    case 0xC: case 0xD: {
        double v = df(vm);
        if (opc2 & 1) s[vd*2] = (int32_t)v;
        else s[vd*2] = (uint32_t)v;
        return;
    }
    }
    fprintf(stderr, "[CPU] Unimpl VFP misc double opc2=%d at 0x%08X\n", opc2, r[15]);
}

} // namespace pt
