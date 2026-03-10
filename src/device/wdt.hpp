#pragma once
#include "../bus.hpp"

namespace pt {

// nRF52 WDT (Watchdog Timer) at 0x40010000
// Emulation: accept reload requests, never timeout
class WDT : public Device {
    uint32_t crv_ = 0xFFFFFFFF;
    uint32_t rren_ = 1;
    uint32_t config_ = 1;
    uint32_t runstatus_ = 0;
    uint32_t reqstatus_ = 0;
    uint32_t inten_ = 0;
    uint32_t ev_timeout_ = 0;

public:
    uint32_t read(uint32_t addr, Width /*w*/) override {
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

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        switch (addr) {
        case 0x000: // TASKS_START
            if (val) runstatus_ = 1;
            break;
        case 0x100: ev_timeout_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x504: crv_ = val; break;
        case 0x508: rren_ = val; break;
        case 0x50C: config_ = val; break;
        }
        // RR[0..7] at 0x600-0x61C: reload request (accept silently)
        if (addr >= 0x600 && addr < 0x620) reqstatus_ |= (1u << ((addr - 0x600) / 4));
    }
};

} // namespace pt
