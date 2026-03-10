#pragma once
#include "bus.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace pt {

class NVIC;

class CPU {
public:
    enum class ShiftType { LSL, LSR, ASR, ROR };

    // Registers
    std::array<uint32_t, 16> r{};   // R0-R15 (R13=SP, R14=LR, R15=PC)
    uint32_t msp = 0, psp = 0;     // Banked stack pointers
    uint32_t xpsr = 0;             // N, Z, C, V, Q, IT, T, exception#

    // Special registers
    uint32_t primask = 0;
    uint32_t basepri = 0;
    uint32_t faultmask = 0;
    uint32_t control = 0;          // [1]=SPSEL, [0]=nPRIV

    // FPU (VFPv4-D16)
    std::array<uint32_t, 32> s{};  // S0-S31 (single-precision, also accessible as D0-D15)
    uint32_t fpscr = 0;           // FP Status and Control

    float& sf(int n) { return reinterpret_cast<float&>(s[n]); }
    double df(int n) { // Read Dn (pair of S2n, S2n+1)
        uint64_t v = s[n*2] | (uint64_t(s[n*2+1]) << 32);
        double d; memcpy(&d, &v, 8); return d;
    }
    void set_df(int n, double v) {
        uint64_t u; memcpy(&u, &v, 8);
        s[n*2] = u; s[n*2+1] = u >> 32;
    }

    uint64_t cycles = 0;
    bool halted = false;
    bool wfi = false;
    bool irq_pending = false;  // Set when any interrupt state changes
    bool breakpoint_hit = false;
    uint32_t prev_pc_ = 0;

    // Simple breakpoint table (up to 16)
    static constexpr int MAX_BP = 16;
    uint32_t breakpoints[MAX_BP]{};
    int num_breakpoints = 0;

    void add_breakpoint(uint32_t addr) {
        if (num_breakpoints < MAX_BP) breakpoints[num_breakpoints++] = addr;
    }

    explicit CPU(Bus* bus) : bus_(bus) {}

    void set_nvic(NVIC* nvic) { nvic_ = nvic; }
    void reset();
    void step();
    void run(uint64_t target_cycles);
    void check_exceptions();

    uint32_t reg(int n) const { return r[n]; }

    // xPSR flag access
    bool flag_n() const { return xpsr & (1u << 31); }
    bool flag_z() const { return xpsr & (1u << 30); }
    bool flag_c() const { return xpsr & (1u << 29); }
    bool flag_v() const { return xpsr & (1u << 28); }

    void set_flags(bool n, bool z, bool c, bool v) {
        xpsr = (xpsr & 0x0FFFFFFF)
             | (n ? (1u<<31) : 0) | (z ? (1u<<30) : 0)
             | (c ? (1u<<29) : 0) | (v ? (1u<<28) : 0);
    }

    void set_nz(uint32_t val) {
        bool n = val >> 31;
        bool z = val == 0;
        xpsr = (xpsr & ~(3u << 30)) | (n ? (1u<<31) : 0) | (z ? (1u<<30) : 0);
    }

    void set_nzc(uint32_t val, bool c) {
        set_nz(val);
        xpsr = (xpsr & ~(1u << 29)) | (c ? (1u<<29) : 0);
    }

    void set_nzcv(uint32_t val, bool c, bool v) {
        set_nzc(val, c);
        xpsr = (xpsr & ~(1u << 28)) | (v ? (1u<<28) : 0);
    }

    // SP access (banked)
    uint32_t sp() const {
        return (control & 2) && !(xpsr & 0xFF) ? psp : msp;
    }

    void set_sp(uint32_t val) {
        if ((control & 2) && !(xpsr & 0xFF)) psp = val & ~3u;
        else msp = val & ~3u;
        r[13] = val & ~3u;
    }

    // Exception state
    bool in_handler_mode() const { return (xpsr & 0x1FF) != 0; }
    uint8_t active_exception() const { return xpsr & 0xFF; }

    // IT block state
    bool in_it_block() const { return (it_state() & 0x0F) != 0; }
    bool last_in_it_block() const { return (it_state() & 0x0F) == 0x08; }
    uint8_t it_cond() const { return (it_state() >> 4) & 0x0F; }

    void advance_it() {
        uint8_t s = it_state();
        if ((s & 0x07) == 0)
            set_it_state(0);
        else
            set_it_state((s & 0xE0) | ((s << 1) & 0x1F));
    }

private:
    Bus* bus_;
    NVIC* nvic_ = nullptr;
    bool branch_taken_ = false;

    uint8_t it_state() const {
        return ((xpsr >> 8) & 0xFC) | ((xpsr >> 25) & 0x03);
    }

    void set_it_state(uint8_t s) {
        xpsr = (xpsr & ~0x0600FC00u)
             | ((s & 0xFC) << 8)
             | ((s & 0x03) << 25);
    }

    bool condition_passed(uint8_t cond) const;
    uint32_t add_with_carry(uint32_t a, uint32_t b, bool cin, bool& cout, bool& vout);
    uint32_t shift_c(uint32_t val, ShiftType type, uint8_t amount, bool& carry);
    uint32_t thumb_expand_imm_c(uint16_t imm12, bool& carry);

    uint32_t read_reg(int n) const;
    void write_reg(int n, uint32_t val);

    // 16-bit Thumb
    void exec_thumb16(uint16_t inst);
    void exec_thumb16_misc(uint16_t inst);

    // 32-bit Thumb dispatch
    void exec_thumb32(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_01(uint16_t hw1, uint16_t hw2, uint8_t op2);
    void exec_thumb32_10(uint16_t hw1, uint16_t hw2, uint8_t op2);
    void exec_thumb32_11(uint16_t hw1, uint16_t hw2, uint8_t op2);

    // 32-bit Thumb instruction groups
    void exec_thumb32_ldst_multiple(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_ldst_dual(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_dp_shifted_reg(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_dp_mod_imm(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_dp_plain_imm(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_branch(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_misc_control(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_ldst_single(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_dp_register(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_multiply(uint16_t hw1, uint16_t hw2);
    void exec_thumb32_long_multiply(uint16_t hw1, uint16_t hw2);

    void exception_entry(uint32_t exception_num);
    void exception_return(uint32_t exc_return);
    void exec_vfp(uint16_t hw1, uint16_t hw2);
    void exec_vfp_ldst(uint16_t hw1, uint16_t hw2);
    void exec_vfp_dp(uint32_t inst);
    void exec_vfp_misc_single(uint32_t inst, uint8_t vd, uint8_t vm);
    void exec_vfp_misc_double(uint32_t inst, uint8_t vd, uint8_t vm);

    void branch_to(uint32_t addr) { r[15] = addr & ~1u; }
};

} // namespace pt
