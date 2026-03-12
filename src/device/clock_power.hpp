#pragma once
#include "../bus.hpp"
#include "ppi.hpp"
#include <cstdio>

namespace pt {

// Combined CLOCK + POWER peripheral (share base 0x40000000, size 0x1000)
// nRF52832: Clocks start immediately on task trigger (no real delay in emulation)
class ClockPower : public Device {
    // CLOCK state
    bool hfclk_running_ = false;
    bool lfclk_running_ = false;
    uint32_t hfclk_src_ = 0;      // 0=RC, 1=XTAL
    uint32_t lfclk_src_ = 0;      // 0=RC, 1=XTAL, 2=Synth
    uint32_t ev_hfclkstarted_ = 0;
    uint32_t ev_lfclkstarted_ = 0;

    // POWER state
    uint32_t resetreas_ = 0;       // Reset reason
    uint32_t ramon_ = 0x00000003;  // RAM on in system ON (default: all on)
    uint32_t ev_pofwarn_ = 0;

    // Interrupt
    uint32_t inten_ = 0;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0; // POWER_CLOCK_IRQn = 0
    PPI* ppi_ = nullptr;

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }
    void set_ppi(PPI* p) { ppi_ = p; }

    uint32_t read(uint32_t addr, Width /*w*/) override {
        switch (addr) {
        // CLOCK events
        case 0x100: return ev_hfclkstarted_;
        case 0x104: return ev_lfclkstarted_;
        // CLOCK status
        case 0x408: return hfclk_running_ ? (0x10001 | (hfclk_src_ << 16)) : 0; // HFCLKSTAT
        case 0x418: return lfclk_running_ ? (0x10001 | (lfclk_src_ << 16)) : 0; // LFCLKSTAT
        case 0x40C: return hfclk_src_;     // HFCLKSRC (not a real register, but some code reads it)
        case 0x518: return lfclk_src_;     // LFCLKSRC

        // POWER events
        case 0x108: return ev_pofwarn_;
        // POWER status
        case 0x400: return resetreas_;
        case 0x428: return ramon_;

        // INTEN
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;

        // POWER MAINREGSTATUS
        case 0x640: return 0; // Main regulator

        default: return 0;
        }
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        switch (addr) {
        // CLOCK tasks
        case 0x000: // TASKS_HFCLKSTART
            if (val) {
                hfclk_running_ = true; ev_hfclkstarted_ = 1;
                if (ppi_) ppi_->on_event(0x40000100); // HFCLKSTARTED → PPI CH31
                update_irq();
            }
            break;
        case 0x004: // TASKS_HFCLKSTOP
            if (val) { hfclk_running_ = false; ev_hfclkstarted_ = 0; }
            break;
        case 0x008: // TASKS_LFCLKSTART
            if (val) { lfclk_running_ = true; ev_lfclkstarted_ = 1; update_irq(); }
            break;
        case 0x00C: // TASKS_LFCLKSTOP
            if (val) { lfclk_running_ = false; ev_lfclkstarted_ = 0; }
            break;

        // CLOCK events (write 0 to clear)
        case 0x100: ev_hfclkstarted_ = val; break;
        case 0x104: ev_lfclkstarted_ = val; break;

        // CLOCK config
        case 0x40C: hfclk_src_ = val & 1; break; // HFCLKSRC (nonstandard offset, but used)
        case 0x518: lfclk_src_ = val & 3; break;  // LFCLKSRC

        // POWER events
        case 0x108: ev_pofwarn_ = val; break;

        // POWER
        case 0x400: resetreas_ &= ~val; break; // RESETREAS (write 1 to clear bits)
        case 0x428: ramon_ = val; break;

        // POWER system OFF
        case 0x500: break; // SYSTEMOFF — ignore

        // INTEN
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;  // INTENSET
        case 0x308: inten_ &= ~val; break; // INTENCLR

        // POWER DCDCEN
        case 0x578: break; // DCDCEN — ignore

        // POWER RAM sections
        default:
            // RAM power registers 0x900-0x93C
            if (addr >= 0x900 && addr < 0x940) break; // ignore
            break;
        }
    }

private:
    void update_irq() {
        if (!nvic_) return;
        bool pending = (inten_ & 1) && ev_hfclkstarted_;
        pending |= (inten_ & 2) && ev_lfclkstarted_;
        if (pending) nvic_->set_pending(irq_);
    }
};

} // namespace pt
