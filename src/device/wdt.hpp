#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <cstdio>

namespace pt {

// nRF52 WDT (Watchdog Timer) at 0x40010000
// WDT clocks at 32768 Hz. CRV = counter reload value.
// Timeout after (CRV+1) / 32768 seconds if not reloaded.
class WDT : public Device {
    uint32_t crv_ = 0xFFFFFFFF;
    uint32_t rren_ = 1;
    uint32_t config_ = 1;
    uint32_t runstatus_ = 0;
    uint32_t reqstatus_ = 0;
    uint32_t inten_ = 0;
    uint32_t ev_timeout_ = 0;

    uint64_t last_reload_ = 0;   // Cycle count at last reload
    bool started_ = false;
    bool* sysreset_ = nullptr;   // Pointer to reset flag

    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    // WDT timeout in CPU cycles: (CRV+1) * (64MHz / 32768Hz) = (CRV+1) * 1953
    uint64_t timeout_cycles() const {
        return (uint64_t)(crv_ + 1) * 1953ULL;
    }

public:
    void set_nvic(NVIC* n, uint32_t irq) { nvic_ = n; irq_ = irq; }
    void set_reset_flag(bool* f) { sysreset_ = f; }

    uint32_t read(uint32_t addr, Width) override {
        switch (addr) {
        case 0x100: return ev_timeout_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;
        case 0x400: return runstatus_;
        case 0x404: return reqstatus_;
        case 0x504: return crv_;
        case 0x508: return rren_;
        case 0x50C: return config_;
        }
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        switch (addr) {
        case 0x000: // TASKS_START
            if (val && !started_) {
                runstatus_ = 1;
                started_ = true;
                // last_reload_ will be set on first tick or RR write
            }
            break;
        case 0x100: ev_timeout_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x504: if (!started_) crv_ = val; break; // CRV locked after start
        case 0x508: if (!started_) rren_ = val; break;
        case 0x50C: if (!started_) config_ = val; break;
        }
        // RR[0..7] at 0x600-0x61C: reload request
        if (addr >= 0x600 && addr < 0x620 && started_) {
            uint32_t rr = (addr - 0x600) / 4;
            if (rren_ & (1u << rr)) {
                reqstatus_ |= (1u << rr);
                // Reset the countdown
                last_reload_ = 0; // Will be set on next tick
            }
        }
    }

    std::optional<Interrupt> tick(uint64_t cycles) override {
        if (!started_) return {};
        if (last_reload_ == 0) { last_reload_ = cycles; return {}; }

        uint64_t elapsed = cycles - last_reload_;
        if (elapsed >= timeout_cycles()) {
            ev_timeout_ = 1;
            fprintf(stderr, "[WDT] Timeout after %.1fs! CRV=%u\n",
                    elapsed / 64e6, crv_);
            if ((inten_ & 1) && nvic_) {
                nvic_->set_pending(irq_);
                return Interrupt{irq_};
            }
            // Two LFCLK cycles after timeout event → reset
            started_ = false;
            runstatus_ = 0;
        }
        return {};
    }

    uint64_t next_event(uint64_t now) const override {
        if (!started_ || last_reload_ == 0) return UINT64_MAX;
        uint64_t deadline = last_reload_ + timeout_cycles();
        return deadline > now ? deadline : now + 1;
    }
};

} // namespace pt
