#pragma once
#include "../bus.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace pt {

// System Control Space: NVIC + SCB + SysTick
// Mapped at 0xE000E000, size 0x1000
class NVIC : public Device {
    // NVIC: 240 external interrupts (nRF52 uses ~48)
    std::array<uint32_t, 8> iser_{};    // Interrupt Set Enable
    std::array<uint32_t, 8> ispr_{};    // Interrupt Set Pending
    std::array<uint32_t, 8> iabr_{};    // Interrupt Active Bit
    std::array<uint8_t, 240> ipr_{};    // Interrupt Priority (8-bit, top 3 bits used on nRF52)

    // System exception pending (bits 2-15: NMI, HardFault, ..., PendSV, SysTick)
    uint16_t sys_pending_ = 0;
    bool sysresetreq_ = false;
    bool* irq_pending_ = nullptr;  // CPU's irq_pending flag

    // SCB
    uint32_t vtor_ = 0;
    uint32_t aircr_ = 0xFA050000;
    uint32_t scr_ = 0;
    uint32_t ccr_ = 0x00000200;   // STKALIGN=1
    std::array<uint8_t, 12> shpr_{}; // System Handler Priority (4-11 mapped to indices 0-7)
    uint32_t shcsr_ = 0;
    uint32_t icsr_ = 0;
    uint32_t cpacr_ = 0;   // Coprocessor Access Control
    uint32_t fpccr_ = 0xC0000000;  // FP Context Control (ASPEN=1, LSPEN=1)
    uint32_t fpcar_ = 0;           // FP Context Address
    uint32_t fpdscr_ = 0;          // FP Default Status Control
    const uint32_t* cpu_pc_ = nullptr;

    // SysTick
    uint32_t syst_csr_ = 0;    // Control and Status
    uint32_t syst_rvr_ = 0;    // Reload Value
    uint32_t syst_cvr_ = 0;    // Current Value
    uint64_t last_tick_ = 0;

public:
    void set_cpu_pc(const uint32_t* pc) { cpu_pc_ = pc; }
    void set_irq_pending_flag(bool* flag) { irq_pending_ = flag; }
    void signal_irq() { if (irq_pending_) *irq_pending_ = true; }

    uint32_t get_vtor() const { return vtor_; }
    bool check_sysreset() {
        if (!sysresetreq_) return false;
        sysresetreq_ = false;
        // Reset SCB state (real hardware resets everything)
        vtor_ = 0;
        aircr_ = 0xFA050000;
        scr_ = 0;
        ccr_ = 0x00000200;
        shcsr_ = 0;
        icsr_ = 0;
        shpr_.fill(0);
        // Reset NVIC
        iser_.fill(0);
        ispr_.fill(0);
        iabr_.fill(0);
        ipr_.fill(0);
        sys_pending_ = 0;
        // Reset SysTick
        syst_csr_ = 0;
        syst_rvr_ = 0;
        syst_cvr_ = 0;
        return true;
    }

    void set_pending(uint32_t irq) {
        if (irq < 240) { ispr_[irq / 32] |= 1u << (irq % 32); signal_irq(); }
    }

    void clear_pending(uint32_t irq) {
        if (irq < 240) ispr_[irq / 32] &= ~(1u << (irq % 32));
    }

    void set_active(uint32_t irq) {
        if (irq < 240) iabr_[irq / 32] |= 1u << (irq % 32);
    }

    void clear_active(uint32_t irq) {
        if (irq < 240) iabr_[irq / 32] &= ~(1u << (irq % 32));
    }

    bool is_enabled(uint32_t irq) const {
        return irq < 240 && (iser_[irq / 32] & (1u << (irq % 32)));
    }

    bool is_pending(uint32_t irq) const {
        return irq < 240 && (ispr_[irq / 32] & (1u << (irq % 32)));
    }

    uint8_t priority(uint32_t irq) const {
        return irq < 240 ? ipr_[irq] : 0xFF;
    }

    // Returns IRQ number of highest-priority pending+enabled interrupt, or -1
    int get_pending_irq() const {
        int best = -1;
        uint8_t best_pri = 0xFF;
        for (uint32_t i = 0; i < 240; i++) {
            if (is_enabled(i) && is_pending(i)) {
                uint8_t pri = ipr_[i];
                if (pri < best_pri) {
                    best_pri = pri;
                    best = static_cast<int>(i);
                }
            }
        }
        return best;
    }

    uint8_t get_pending_priority() const {
        uint8_t best = 0xFF;
        for (uint32_t i = 0; i < 240; i++)
            if (is_enabled(i) && is_pending(i))
                best = std::min(best, ipr_[i]);
        return best;
    }

    // System exception priorities (HardFault=-1 fixed, others configurable)
    uint8_t system_priority(uint32_t exception_num) const {
        // Exception numbers 4-15 map to shpr_[exception_num - 4]
        if (exception_num >= 4 && exception_num <= 15)
            return shpr_[exception_num - 4];
        if (exception_num == 2) return static_cast<uint8_t>(-2); // NMI
        if (exception_num == 3) return static_cast<uint8_t>(-1); // HardFault
        return 0;
    }

    // System exception management
    void set_sys_pending(uint32_t exc) { if (exc >= 2 && exc <= 15) { sys_pending_ |= (1u << exc); signal_irq(); } }
    void clear_sys_pending(uint32_t exc) { if (exc >= 2 && exc <= 15) sys_pending_ &= ~(1u << exc); }
    bool is_sys_pending(uint32_t exc) const { return exc >= 2 && exc <= 15 && (sys_pending_ & (1u << exc)); }

    bool systick_enabled() const { return syst_csr_ & 1; }
    bool systick_irq_enabled() const { return syst_csr_ & 2; }
    bool systick_uses_cpu_clock() const { return syst_csr_ & 4; }

    // Register access — offset from 0xE000E000
    uint32_t read(uint32_t addr, Width /*w*/) override {
        // SysTick: 0x010-0x01C
        if (addr >= 0x010 && addr <= 0x01C) return read_systick(addr - 0x010);
        // NVIC: 0x100-0x4FF
        if (addr >= 0x100 && addr < 0x500) return read_nvic(addr - 0x100);
        // SCB: 0xD00-0xD8C (includes CPACR at 0xD88)
        if (addr >= 0xD00 && addr < 0xD90) return read_scb(addr - 0xD00);
        // FP extension: 0xF34-0xF3C
        if (addr >= 0xF34 && addr <= 0xF3C) return read_scb(addr - 0xD00);
        // STIR: 0xF00
        if (addr == 0xF00) return 0;
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        if (addr >= 0x010 && addr <= 0x01C) { write_systick(addr - 0x010, val); return; }
        if (addr >= 0x100 && addr < 0x500) { write_nvic(addr - 0x100, val); return; }
        if (addr >= 0xD00 && addr < 0xD90) { write_scb(addr - 0xD00, val); return; }
        if (addr >= 0xF34 && addr <= 0xF3C) { write_scb(addr - 0xD00, val); return; }
        if (addr == 0xF00) { set_pending(val & 0xFF); return; } // STIR
    }

    std::optional<Interrupt> tick(uint64_t cycles) override {
        if (!(syst_csr_ & 1)) return {};
        uint64_t elapsed = cycles - last_tick_;
        if (elapsed == 0) return {};
        last_tick_ = cycles;

        if (syst_cvr_ == 0) {
            syst_cvr_ = syst_rvr_;
            syst_csr_ |= (1 << 16); // COUNTFLAG
            if (syst_csr_ & 2) set_sys_pending(15); // SysTick = exception 15
        } else if (elapsed >= syst_cvr_) {
            syst_cvr_ = syst_rvr_ - (elapsed - syst_cvr_);
            syst_csr_ |= (1 << 16);
            if (syst_csr_ & 2) set_sys_pending(15);
        } else {
            syst_cvr_ -= elapsed;
        }
        return {};
    }

private:
    uint32_t read_systick(uint32_t off) const {
        switch (off) {
        case 0x00: return syst_csr_;
        case 0x04: return syst_rvr_;
        case 0x08: return syst_cvr_;
        case 0x0C: return 0x00FFFFFF; // CALIB: 24-bit timer, no TENMS
        }
        return 0;
    }

    void write_systick(uint32_t off, uint32_t val) {
        switch (off) {
        case 0x00: syst_csr_ = val & 0x7; break;
        case 0x04: syst_rvr_ = val & 0x00FFFFFF; break;
        case 0x08: syst_cvr_ = 0; syst_csr_ &= ~(1u << 16); break; // Write clears
        }
    }

    uint32_t read_nvic(uint32_t off) const {
        if (off < 0x020) return iser_[off / 4];                          // ISER0-7
        if (off >= 0x080 && off < 0x0A0) return ~iser_[(off-0x080)/4];   // ICER (read as inverse)
        if (off >= 0x100 && off < 0x120) return ispr_[(off-0x100)/4];    // ISPR
        if (off >= 0x180 && off < 0x1A0) { (void)0; return 0; }         // ICPR (handled in write)
        if (off >= 0x200 && off < 0x220) return iabr_[(off-0x200)/4];    // IABR
        if (off >= 0x300 && off < 0x3F0) {                               // IPR
            uint32_t idx = off - 0x300;
            if (idx + 3 < 240)
                return ipr_[idx] | (ipr_[idx+1] << 8) |
                       (ipr_[idx+2] << 16) | (ipr_[idx+3] << 24);
        }
        return 0;
    }

    void write_nvic(uint32_t off, uint32_t val) {
        if (off < 0x020) { iser_[off / 4] |= val; return; }                         // ISER: set bits
        if (off >= 0x080 && off < 0x0A0) { iser_[(off-0x080)/4] &= ~val; return; }  // ICER: clear bits
        if (off >= 0x100 && off < 0x120) { ispr_[(off-0x100)/4] |= val; signal_irq(); return; }   // ISPR: set pending
        if (off >= 0x180 && off < 0x1A0) { ispr_[(off-0x180)/4] &= ~val; return; }  // ICPR: clear pending
        if (off >= 0x300 && off < 0x3F0) {                                           // IPR
            uint32_t idx = off - 0x300;
            if (idx + 3 < 240) {
                ipr_[idx]   = val;
                ipr_[idx+1] = val >> 8;
                ipr_[idx+2] = val >> 16;
                ipr_[idx+3] = val >> 24;
            }
        }
    }

    uint32_t read_scb(uint32_t off) const {
        switch (off) {
        case 0x00: return 0x410FC241;  // CPUID: Cortex-M4 r0p1
        case 0x04: return icsr_;
        case 0x08: return vtor_;
        case 0x0C: return aircr_;
        case 0x10: return scr_;
        case 0x14: return ccr_;
        case 0x18: return shpr_[0] | (shpr_[1]<<8) | (shpr_[2]<<16) | (shpr_[3]<<24);
        case 0x1C: return shpr_[4] | (shpr_[5]<<8) | (shpr_[6]<<16) | (shpr_[7]<<24);
        case 0x20: return shpr_[8] | (shpr_[9]<<8) | (shpr_[10]<<16) | (shpr_[11]<<24);
        case 0x24: return shcsr_;
        case 0x28: return 0; // CFSR
        case 0x2C: return 0; // HFSR
        case 0x30: return 0; // DFSR
        case 0x34: return 0; // MMFAR
        case 0x38: return 0; // BFAR
        case 0x3C: return 0; // AFSR
        case 0x88: return cpacr_;
        // FP extension registers (at 0xF34, 0xF38, 0xF3C offset from E000ED00)
        case 0x34 + 0x100: return fpccr_;
        case 0x38 + 0x100: return fpcar_;
        case 0x3C + 0x100: return fpdscr_;
        }
        return 0;
    }

    bool fpu_enabled() const { return (cpacr_ & 0x00F00000) != 0; }

    void write_scb(uint32_t off, uint32_t val) {
        switch (off) {
        case 0x04: // ICSR — write to clear/set pending
                if (val & (1u << 28)) {
                    set_sys_pending(14); // PENDSVSET
                }
            if (val & (1u << 27)) clear_sys_pending(14); // PENDSVCLR
            if (val & (1u << 26)) set_sys_pending(15); // PENDSTSET
            if (val & (1u << 25)) clear_sys_pending(15); // PENDSTCLR
            break;
        case 0x08:
            vtor_ = val & 0xFFFFFF80;
            fprintf(stderr, "[NVIC] VTOR = 0x%08X (raw=0x%08X) PC=0x%08X\n",
                    vtor_, val, cpu_pc_ ? *cpu_pc_ : 0xDEAD);
            break;
        case 0x0C:
            if ((val >> 16) == 0x05FA) {
                aircr_ = (val & 0x0700) | 0xFA050000;
                if (val & 0x04) {
                    sysresetreq_ = true;
                    fprintf(stderr, "[NVIC] SYSRESETREQ at PC=0x%08X\n", cpu_pc_ ? *cpu_pc_ : 0xDEAD);
                }
            }
            break;
        case 0x10: scr_ = val & 0x1E; break;
        case 0x14: ccr_ = val; break;
        case 0x18:
            shpr_[0] = val; shpr_[1] = val>>8; shpr_[2] = val>>16; shpr_[3] = val>>24;
            break;
        case 0x1C:
            shpr_[4] = val; shpr_[5] = val>>8; shpr_[6] = val>>16; shpr_[7] = val>>24;
            break;
        case 0x20:
            shpr_[8] = val; shpr_[9] = val>>8; shpr_[10] = val>>16; shpr_[11] = val>>24;
            break;
        case 0x24: shcsr_ = val; break;
        case 0x88: cpacr_ = val; break;
        case 0x34 + 0x100: fpccr_ = val; break;
        case 0x38 + 0x100: fpcar_ = val; break;
        case 0x3C + 0x100: fpdscr_ = val; break;
        }
    }
};

} // namespace pt
