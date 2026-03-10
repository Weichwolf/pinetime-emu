#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <array>

namespace pt {

// nRF52 SAADC (Successive Approximation ADC) at 0x40007000
// PineTime uses channel 0 for battery voltage (AIN7, 1/5 gain, internal ref)
class SAADC : public Device {
    uint32_t ev_started_ = 0;
    uint32_t ev_end_ = 0;
    uint32_t ev_done_ = 0;
    uint32_t ev_resultdone_ = 0;
    std::array<uint32_t, 8> ev_ch_limith_{};
    std::array<uint32_t, 8> ev_ch_limitl_{};
    uint32_t ev_calibratedone_ = 0;
    uint32_t ev_stopped_ = 0;

    uint32_t inten_ = 0;
    uint32_t enable_ = 0;

    // Channel config (8 channels)
    struct ChConfig {
        uint32_t pselp = 0;   // Positive input
        uint32_t pseln = 0;   // Negative input
        uint32_t config = 0;  // Gain, refsel, tacq, mode, burst
        uint32_t limit = 0;
    };
    std::array<ChConfig, 8> ch_{};

    uint32_t resolution_ = 2;  // 0=8bit, 1=10bit, 2=12bit, 3=14bit
    uint32_t oversample_ = 0;
    uint32_t samplerate_ = 0;

    // DMA
    uint32_t result_ptr_ = 0;
    uint32_t result_maxcnt_ = 0;
    uint32_t result_amount_ = 0;

    Bus* bus_ = nullptr;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    // Simulated battery: ~4.0V through voltage divider = ~0.8V at ADC
    // With 1/5 gain and 0.6V ref: raw = 0.8 * (1/5) / 0.6 * 4095 ≈ 1092
    static constexpr int16_t BATTERY_ADC = 1092;

public:
    void set_bus(Bus* bus) { bus_ = bus; }
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    uint32_t read(uint32_t addr, Width) override {
        // Events
        if (addr == 0x100) return ev_started_;
        if (addr == 0x104) return ev_end_;
        if (addr == 0x108) return ev_done_;
        if (addr == 0x10C) return ev_resultdone_;
        if (addr >= 0x118 && addr < 0x158) {
            uint32_t idx = (addr - 0x118) / 8;
            return ((addr - 0x118) % 8 == 0) ? ev_ch_limith_[idx] : ev_ch_limitl_[idx];
        }
        if (addr == 0x160) return ev_calibratedone_;
        if (addr == 0x164) return ev_stopped_;

        if (addr == 0x300) return inten_;
        if (addr == 0x304) return inten_;
        if (addr == 0x500) return enable_;

        // Channel config
        if (addr >= 0x510 && addr < 0x590) {
            uint32_t ch = (addr - 0x510) / 16;
            uint32_t off = (addr - 0x510) % 16;
            if (ch < 8) {
                switch (off) {
                case 0: return ch_[ch].pselp;
                case 4: return ch_[ch].pseln;
                case 8: return ch_[ch].config;
                case 12: return ch_[ch].limit;
                }
            }
        }

        if (addr == 0x5F0) return resolution_;
        if (addr == 0x5F4) return oversample_;
        if (addr == 0x5F8) return samplerate_;
        if (addr == 0x62C) return result_ptr_;
        if (addr == 0x630) return result_maxcnt_;
        if (addr == 0x634) return result_amount_;
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        // Tasks
        switch (addr) {
        case 0x000: if (val) do_start(); return;
        case 0x004: if (val) do_sample(); return;
        case 0x008: if (val) do_stop(); return;
        case 0x00C: if (val) do_calibrate(); return;
        }

        // Events
        if (addr == 0x100) { ev_started_ = val; return; }
        if (addr == 0x104) { ev_end_ = val; return; }
        if (addr == 0x108) { ev_done_ = val; return; }
        if (addr == 0x10C) { ev_resultdone_ = val; return; }
        if (addr == 0x160) { ev_calibratedone_ = val; return; }
        if (addr == 0x164) { ev_stopped_ = val; return; }

        if (addr == 0x300) { inten_ = val; return; }
        if (addr == 0x304) { inten_ |= val; return; }
        if (addr == 0x308) { inten_ &= ~val; return; }
        if (addr == 0x500) { enable_ = val & 1; return; }

        // Channel config
        if (addr >= 0x510 && addr < 0x590) {
            uint32_t ch = (addr - 0x510) / 16;
            uint32_t off = (addr - 0x510) % 16;
            if (ch < 8) {
                switch (off) {
                case 0: ch_[ch].pselp = val; break;
                case 4: ch_[ch].pseln = val; break;
                case 8: ch_[ch].config = val; break;
                case 12: ch_[ch].limit = val; break;
                }
            }
            return;
        }

        if (addr == 0x5F0) { resolution_ = val & 3; return; }
        if (addr == 0x5F4) { oversample_ = val; return; }
        if (addr == 0x5F8) { samplerate_ = val; return; }
        if (addr == 0x62C) { result_ptr_ = val; return; }
        if (addr == 0x630) { result_maxcnt_ = val; return; }
    }

private:
    void do_start() {
        ev_started_ = 1;
        fire_if(0); // STARTED event = bit 0
    }

    void do_sample() {
        if (!bus_ || !enable_) return;
        uint32_t written = 0;
        for (uint32_t ch = 0; ch < 8 && written < result_maxcnt_; ch++) {
            if (ch_[ch].pselp == 0) continue; // Not connected
            int16_t val = BATTERY_ADC; // All channels return battery-like value
            bus_->write16(result_ptr_ + written * 2, static_cast<uint16_t>(val));
            written++;
        }
        result_amount_ = written;
        ev_done_ = 1;
        ev_resultdone_ = 1;
        ev_end_ = 1;
        fire_if(1); // END
        fire_if(2); // DONE
        fire_if(3); // RESULTDONE
    }

    void do_stop() {
        ev_stopped_ = 1;
        fire_if(6);
    }

    void do_calibrate() {
        ev_calibratedone_ = 1;
        fire_if(5);
    }

    void fire_if(uint32_t bit) {
        if ((inten_ & (1u << bit)) && nvic_)
            nvic_->set_pending(irq_);
    }
};

} // namespace pt
