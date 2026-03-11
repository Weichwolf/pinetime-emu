#pragma once
#include "../bus.hpp"
#include "nvic.hpp"

namespace pt {

// nRF52 TEMP (Temperature Sensor) at 0x4000C000
// Returns die temperature in 0.25°C units
class TEMP : public Device {
    uint32_t ev_datardy_ = 0;
    uint32_t inten_ = 0;
    int32_t  temp_ = 84;  // 21.0°C = 84 * 0.25°C
    bool     running_ = false;
    uint32_t delay_ = 0;

    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    static constexpr uint32_t TEMP_DELAY = 36; // ~0.6µs conversion

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    uint32_t read(uint32_t addr, Width) override {
        switch (addr) {
        case 0x100: return ev_datardy_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x508: return static_cast<uint32_t>(temp_);
        }
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        switch (addr) {
        case 0x000: if (val) { running_ = true; delay_ = TEMP_DELAY; } break;
        case 0x004: if (val) running_ = false; break;
        case 0x100: ev_datardy_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        // A0-A5: calibration coefficients — ignore
        }
    }

    std::optional<Interrupt> tick(uint64_t) override {
        if (!running_) return {};
        if (delay_ > 0) { delay_--; return {}; }
        ev_datardy_ = 1;
        running_ = false;
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
