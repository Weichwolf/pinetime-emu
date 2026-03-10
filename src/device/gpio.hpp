#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <array>
#include <functional>

namespace pt {

class GPIOTE;

// GPIO Port (P0) at 0x50000000, size 0x1000
// 32 pins, each with individual configuration
class GPIO : public Device {
    uint32_t out_ = 0;           // Output values
    uint32_t in_ = 0;            // Input values (simulated externally)
    uint32_t dir_ = 0;           // Direction (1=output)
    uint32_t latch_ = 0;         // Latch (detect state)
    std::array<uint32_t, 32> pin_cnf_{}; // Per-pin config

    // Callbacks
    std::function<void(uint32_t old_out, uint32_t new_out)> on_out_change_;
    GPIOTE* gpiote_ = nullptr;

    void update_out(uint32_t new_out) {
        if (new_out != out_ && on_out_change_) on_out_change_(out_, new_out);
        out_ = new_out;
    }

    inline void notify_gpiote();

public:
    void set_on_out_change(std::function<void(uint32_t, uint32_t)> cb) { on_out_change_ = cb; }
    void set_gpiote(GPIOTE* g) { gpiote_ = g; }

    // External interface for simulating inputs
    void set_pin(uint8_t pin, bool high) {
        if (pin >= 32) return;
        if (high) in_ |= (1u << pin);
        else in_ &= ~(1u << pin);
        // Update LATCH: set bit when SENSE condition matches
        uint8_t sense = (pin_cnf_[pin] >> 16) & 3;
        if ((sense == 2 && high) || (sense == 3 && !high))
            latch_ |= (1u << pin);
        notify_gpiote();
    }

    bool get_pin(uint8_t pin) const {
        if (pin >= 32) return false;
        if (dir_ & (1u << pin)) return out_ & (1u << pin); // Output
        return in_ & (1u << pin); // Input
    }

    uint32_t get_out() const { return out_; }
    uint32_t pin_cnf(int pin) const { return pin < 32 ? pin_cnf_[pin] : 0; }
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
        if (addr >= 0x700 && addr < 0x780) {
            uint8_t pin = (addr - 0x700) / 4;
            uint32_t val = pin_cnf_[pin];
            // Emulate nrfx_gpiote SENSE toggle: when ISR reads PIN_CNF and
            // SENSE matches current pin state, auto-toggle SENSE for next edge.
            // The firmware binary's nrfx_gpiote should do this but doesn't.
            uint8_t sense = (val >> 16) & 3;
            if (sense >= 2) {
                bool pin_high = get_pin(pin);
                if ((sense == 2 && pin_high) || (sense == 3 && !pin_high)) {
                    uint8_t new_sense = (sense == 2) ? 3 : 2;
                    pin_cnf_[pin] = (val & ~0x00030000u) | ((uint32_t)new_sense << 16);
                }
            }
            return val;  // Return ORIGINAL value to firmware
        }
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
            if (addr >= 0x700 && addr < 0x780) {
                pin_cnf_[(addr - 0x700) / 4] = val;
            }
            break;
        }
    }
};

// GPIOTE (GPIO Tasks and Events) at 0x40006000, size 0x1000
// Monitors GPIO pin edges and generates interrupts
class GPIOTE : public Device {
    std::array<uint32_t, 8> config_{}; // Channel config (MODE, PSEL, POLARITY)
    std::array<uint32_t, 8> ev_in_{};  // Input events per channel
    uint32_t ev_port_ = 0;            // PORT event (SENSE-based)
    uint32_t inten_ = 0;
    uint32_t pin_state_ = 0;          // Last observed GPIO pin state

    GPIO* gpio_ = nullptr;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    void fire_irq() {
        if (!nvic_) return;
        bool pending = false;
        for (int i = 0; i < 8; i++)
            pending |= (inten_ & (1u << i)) && ev_in_[i];
        pending |= (inten_ & (1u << 31)) && ev_port_;
        if (pending) nvic_->set_pending(irq_);
    }

public:
    void set_gpio(GPIO* gpio) { gpio_ = gpio; }
    uint32_t get_ev_port() const { return ev_port_; }
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    // Called when any GPIO input pin changes (from GPIO::set_pin or output changes)
    void pin_changed(uint32_t new_state) {
        uint32_t old = pin_state_;
        pin_state_ = new_state;
        uint32_t rising = new_state & ~old;
        uint32_t falling = old & ~new_state;

        for (int ch = 0; ch < 8; ch++) {
            uint32_t cfg = config_[ch];
            uint8_t mode = cfg & 3;
            if (mode != 1) continue; // Only Event mode
            uint8_t pin = (cfg >> 8) & 0x1F;
            uint8_t pol = (cfg >> 16) & 3;
            bool triggered = false;
            if (pol == 1) triggered = (rising & (1u << pin));   // LoToHi
            else if (pol == 2) triggered = (falling & (1u << pin)); // HiToLo
            else if (pol == 3) triggered = ((rising | falling) & (1u << pin)); // Toggle
            if (triggered) {
                ev_in_[ch] = 1;
                fire_irq();
            }
        }

        // PORT event: fire on any input change
        if (rising | falling) {
            ev_port_ = 1;
            fire_irq();
        }
    }

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
        // TASKS_OUT[0..7]: toggle pin (Task mode)
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

inline void GPIO::notify_gpiote() {
    if (gpiote_) {
        uint32_t combined = (in_ & ~dir_) | (out_ & dir_);
        gpiote_->pin_changed(combined);
    }
}

} // namespace pt
