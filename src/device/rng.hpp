#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <cstdlib>

namespace pt {

// nRF52 RNG (Random Number Generator) at 0x4000D000
class RNG : public Device {
    uint32_t ev_valrdy_ = 0;
    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;
    uint32_t config_ = 0;  // bit 0: DERCEN (bias correction)
    uint8_t  value_ = 0;
    bool     running_ = false;
    uint32_t delay_ = 0;   // Cycles until next value ready

    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    static constexpr uint32_t RNG_DELAY = 128; // ~2µs at 64MHz

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    uint32_t read(uint32_t addr, Width) override {
        switch (addr) {
        case 0x100: return ev_valrdy_;
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x504: return config_;
        case 0x508: return value_;
        }
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        switch (addr) {
        case 0x000: // TASKS_START
            if (val) { running_ = true; delay_ = RNG_DELAY; }
            break;
        case 0x004: // TASKS_STOP
            if (val) running_ = false;
            break;
        case 0x100: ev_valrdy_ = val; break;
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x504: config_ = val & 1; break;
        }
    }

    std::optional<Interrupt> tick(uint64_t) override {
        if (!running_) return {};
        if (delay_ > 0) { delay_--; return {}; }

        // Generate random byte
        value_ = rand() & 0xFF;
        ev_valrdy_ = 1;
        delay_ = RNG_DELAY;

        // SHORT: VALRDY_STOP
        if (shorts_ & 1) running_ = false;

        if ((inten_ & 1) && nvic_) {
            nvic_->set_pending(irq_);
            return Interrupt{irq_};
        }
        return {};
    }

    uint64_t next_event(uint64_t now) const override {
        if (!running_) return UINT64_MAX;
        return now + delay_ + 1;
    }
};

} // namespace pt
