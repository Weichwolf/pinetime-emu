#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <array>

namespace pt {

// nRF52 EGU (Event Generator Unit) / SWI
// EGU0-EGU5 at 0x40014000-0x40019000, IRQ 20-25
// NimBLE uses these to signal from ISR to task context.
class EGU : public Device {
    std::array<uint32_t, 16> ev_triggered_{};
    uint32_t inten_ = 0;

    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    void fire_irq() {
        if (!nvic_) return;
        for (int i = 0; i < 16; i++) {
            if ((inten_ & (1u << i)) && ev_triggered_[i]) {
                nvic_->set_pending(irq_);
                return;
            }
        }
    }

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    uint32_t read(uint32_t addr, Width) override {
        // EVENTS_TRIGGERED[0..15] at 0x100-0x13C
        if (addr >= 0x100 && addr < 0x140)
            return ev_triggered_[(addr - 0x100) / 4];
        if (addr == 0x300) return inten_;
        if (addr == 0x304) return inten_;
        if (addr == 0x308) return inten_;
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        // TASKS_TRIGGER[0..15] at 0x000-0x03C
        if (addr < 0x040) {
            if (val) {
                int ch = addr / 4;
                ev_triggered_[ch] = 1;
                fire_irq();
            }
            return;
        }
        // EVENTS_TRIGGERED[0..15]
        if (addr >= 0x100 && addr < 0x140) {
            ev_triggered_[(addr - 0x100) / 4] = val;
            return;
        }
        if (addr == 0x300) { inten_ = val; return; }
        if (addr == 0x304) { inten_ |= val; return; }
        if (addr == 0x308) { inten_ &= ~val; return; }
    }
};

} // namespace pt
