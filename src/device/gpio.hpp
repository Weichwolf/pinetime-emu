#pragma once
#include "../bus.hpp"
#include <array>
#include <cstdio>
#include <functional>

namespace pt {

// GPIO Port (P0) at 0x50000000, size 0x1000
// 32 pins, each with individual configuration
class GPIO : public Device {
    uint32_t out_ = 0;           // Output values
    uint32_t in_ = 0;            // Input values (simulated externally)
    uint32_t dir_ = 0;           // Direction (1=output)
    uint32_t latch_ = 0;         // Latch (detect state)
    std::array<uint32_t, 32> pin_cnf_{}; // Per-pin config

    // Pin change callback: called when output pins change
    std::function<void(uint32_t old_out, uint32_t new_out)> on_out_change_;

    void update_out(uint32_t new_out) {
        if (new_out != out_ && on_out_change_) on_out_change_(out_, new_out);
        out_ = new_out;
    }

public:
    void set_on_out_change(std::function<void(uint32_t, uint32_t)> cb) { on_out_change_ = cb; }
    // External interface for simulating inputs
    void set_pin(uint8_t pin, bool high) {
        if (pin >= 32) return;
        if (high) in_ |= (1u << pin);
        else in_ &= ~(1u << pin);
    }

    bool get_pin(uint8_t pin) const {
        if (pin >= 32) return false;
        if (dir_ & (1u << pin)) return out_ & (1u << pin); // Output
        return in_ & (1u << pin); // Input
    }

    uint32_t get_out() const { return out_; }
    uint32_t get_dir() const { return dir_; }

    uint32_t read(uint32_t addr, Width /*w*/) override {
        switch (addr) {
        case 0x504: return out_;
        case 0x508: return out_; // OUTSET reads as OUT
        case 0x50C: return out_; // OUTCLR reads as OUT
        case 0x510: return (in_ & ~dir_) | (out_ & dir_); // IN
        case 0x514: return dir_;
        case 0x518: return dir_; // DIRSET reads as DIR
        case 0x51C: return dir_; // DIRCLR reads as DIR
        case 0x520: return latch_;
        case 0x524: return 0; // DETECTMODE
        }
        // PIN_CNF[0..31] at 0x700-0x77C
        if (addr >= 0x700 && addr < 0x780)
            return pin_cnf_[(addr - 0x700) / 4];
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        switch (addr) {
        case 0x504: update_out(val); break;
        case 0x508: update_out(out_ | val); break;       // OUTSET
        case 0x50C: update_out(out_ & ~val); break;      // OUTCLR
        case 0x514: dir_ = val; break;
        case 0x518: dir_ |= val; break;       // DIRSET
        case 0x51C: dir_ &= ~val; break;      // DIRCLR
        case 0x520: latch_ &= ~val; break;    // LATCH (write 1 to clear)
        case 0x524: break; // DETECTMODE
        default:
            if (addr >= 0x700 && addr < 0x780)
                pin_cnf_[(addr - 0x700) / 4] = val;
            break;
        }
    }
};

// GPIOTE (GPIO Tasks and Events) at 0x40006000, size 0x1000
class GPIOTE : public Device {
    std::array<uint32_t, 8> config_{}; // Channel config
    std::array<uint32_t, 8> ev_in_{};  // Input events
    uint32_t ev_port_ = 0;
    uint32_t inten_ = 0;

public:
    uint32_t read(uint32_t addr, Width /*w*/) override {
        if (addr >= 0x100 && addr < 0x120) return ev_in_[(addr - 0x100) / 4];
        if (addr == 0x17C) return ev_port_;
        if (addr >= 0x510 && addr < 0x530) return config_[(addr - 0x510) / 4];
        if (addr == 0x300) return inten_;
        if (addr == 0x304) return inten_;
        if (addr == 0x308) return inten_;
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        // TASKS_OUT[0..7]
        if (addr < 0x020) return;
        // TASKS_SET[0..7]
        if (addr >= 0x030 && addr < 0x050) return;
        // TASKS_CLR[0..7]
        if (addr >= 0x060 && addr < 0x080) return;

        if (addr >= 0x100 && addr < 0x120) { ev_in_[(addr - 0x100) / 4] = val; return; }
        if (addr == 0x17C) { ev_port_ = val; return; }
        if (addr >= 0x510 && addr < 0x530) { config_[(addr - 0x510) / 4] = val; return; }
        if (addr == 0x300) { inten_ = val; return; }
        if (addr == 0x304) { inten_ |= val; return; }
        if (addr == 0x308) { inten_ &= ~val; return; }
    }
};

} // namespace pt
