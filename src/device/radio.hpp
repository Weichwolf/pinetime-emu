#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <array>

namespace pt {

// nRF52 RADIO at 0x40001000
// BLE radio — NimBLE drives this directly
// For emulation: store config, complete tasks instantly, never receive packets
class Radio : public Device {
    // Events
    uint32_t ev_ready_ = 0;
    uint32_t ev_address_ = 0;
    uint32_t ev_payload_ = 0;
    uint32_t ev_end_ = 0;
    uint32_t ev_disabled_ = 0;
    uint32_t ev_devmatch_ = 0;
    uint32_t ev_devmiss_ = 0;
    uint32_t ev_rssiend_ = 0;
    uint32_t ev_bcmatch_ = 0;
    uint32_t ev_crcok_ = 0;
    uint32_t ev_crcerror_ = 0;

    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;

    // Config registers
    uint32_t packetptr_ = 0;
    uint32_t frequency_ = 0;
    uint32_t txpower_ = 0;
    uint32_t mode_ = 0;
    uint32_t pcnf0_ = 0;
    uint32_t pcnf1_ = 0;
    uint32_t base0_ = 0, base1_ = 0;
    uint32_t prefix0_ = 0, prefix1_ = 0;
    uint32_t txaddress_ = 0;
    uint32_t rxaddresses_ = 0;
    uint32_t crccnf_ = 0;
    uint32_t crcpoly_ = 0;
    uint32_t crcinit_ = 0;
    uint32_t tifs_ = 0;
    uint32_t rssisample_ = 0xFFFFFFFF; // No signal
    uint32_t state_ = 0;  // 0=Disabled
    uint32_t datawhiteiv_ = 0;
    std::array<uint32_t, 8> bcc_{};
    std::array<uint32_t, 8> dab_{};
    std::array<uint32_t, 8> dap_{};
    uint32_t dacnf_ = 0;
    uint32_t modecnf0_ = 0;
    uint32_t power_ = 1;

    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    uint32_t read(uint32_t addr, Width) override {
        switch (addr) {
        case 0x100: return ev_ready_;
        case 0x104: return ev_address_;
        case 0x108: return ev_payload_;
        case 0x10C: return ev_end_;
        case 0x110: return ev_disabled_;
        case 0x114: return ev_devmatch_;
        case 0x118: return ev_devmiss_;
        case 0x11C: return ev_rssiend_;
        case 0x128: return ev_bcmatch_;
        case 0x130: return ev_crcok_;
        case 0x134: return ev_crcerror_;
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x504: return packetptr_;
        case 0x508: return frequency_;
        case 0x50C: return txpower_;
        case 0x510: return mode_;
        case 0x514: return pcnf0_;
        case 0x518: return pcnf1_;
        case 0x51C: return base0_;
        case 0x520: return base1_;
        case 0x524: return prefix0_;
        case 0x528: return prefix1_;
        case 0x52C: return txaddress_;
        case 0x530: return rxaddresses_;
        case 0x534: return crccnf_;
        case 0x538: return crcpoly_;
        case 0x53C: return crcinit_;
        case 0x544: return tifs_;
        case 0x548: return rssisample_;
        case 0x550: return state_;
        case 0x554: return datawhiteiv_;
        case 0x650: return dacnf_;
        case 0x5B0: return modecnf0_;
        case 0xFFC: return power_;
        }
        // DAB[n] at 0x600+n*4
        if (addr >= 0x600 && addr < 0x620) return dab_[(addr-0x600)/4];
        // DAP[n] at 0x620+n*4
        if (addr >= 0x620 && addr < 0x640) return dap_[(addr-0x620)/4];
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        switch (addr) {
        // Tasks
        case 0x000: if (val) do_txen(); break;
        case 0x004: if (val) do_rxen(); break;
        case 0x008: if (val) do_start(); break;
        case 0x00C: if (val) do_stop(); break;
        case 0x010: if (val) do_disable(); break;
        case 0x014: break; // RSSISTART
        case 0x018: break; // RSSISTOP
        case 0x01C: break; // BCSTART
        case 0x020: break; // BCSTOP
        case 0x024: break; // EDSTART
        case 0x028: break; // EDSTOP
        case 0x02C: break; // CCASTART
        case 0x030: break; // CCASTOP
        // Events
        case 0x100: ev_ready_ = val; break;
        case 0x104: ev_address_ = val; break;
        case 0x108: ev_payload_ = val; break;
        case 0x10C: ev_end_ = val; break;
        case 0x110: ev_disabled_ = val; break;
        case 0x114: ev_devmatch_ = val; break;
        case 0x118: ev_devmiss_ = val; break;
        case 0x11C: ev_rssiend_ = val; break;
        case 0x128: ev_bcmatch_ = val; break;
        case 0x130: ev_crcok_ = val; break;
        case 0x134: ev_crcerror_ = val; break;
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x504: packetptr_ = val; break;
        case 0x508: frequency_ = val; break;
        case 0x50C: txpower_ = val; break;
        case 0x510: mode_ = val; break;
        case 0x514: pcnf0_ = val; break;
        case 0x518: pcnf1_ = val; break;
        case 0x51C: base0_ = val; break;
        case 0x520: base1_ = val; break;
        case 0x524: prefix0_ = val; break;
        case 0x528: prefix1_ = val; break;
        case 0x52C: txaddress_ = val; break;
        case 0x530: rxaddresses_ = val; break;
        case 0x534: crccnf_ = val; break;
        case 0x538: crcpoly_ = val; break;
        case 0x53C: crcinit_ = val; break;
        case 0x544: tifs_ = val; break;
        case 0x554: datawhiteiv_ = val; break;
        case 0x650: dacnf_ = val; break;
        case 0x5B0: modecnf0_ = val; break;
        case 0xFFC: power_ = val & 1; break;
        }
        if (addr >= 0x600 && addr < 0x620) { dab_[(addr-0x600)/4] = val; return; }
        if (addr >= 0x620 && addr < 0x640) { dap_[(addr-0x620)/4] = val; return; }
    }

private:
    void fire_irq() {
        if (!nvic_) return;
        bool pending = false;
        pending |= (inten_ & (1<<0)) && ev_ready_;
        pending |= (inten_ & (1<<1)) && ev_address_;
        pending |= (inten_ & (1<<2)) && ev_payload_;
        pending |= (inten_ & (1<<3)) && ev_end_;
        pending |= (inten_ & (1<<4)) && ev_disabled_;
        if (pending) nvic_->set_pending(irq_);
    }

    // nRF52 RADIO states
    enum State {
        DISABLED = 0, RXRU = 1, RXIDLE = 2, RX = 3, RXDISABLE = 4,
        TXRU = 9, TXIDLE = 10, TX = 11, TXDISABLE = 12
    };

    void do_txen() {
        state_ = TXRU;
        // Instant ramp-up (no timing)
        state_ = TXIDLE;
        ev_ready_ = 1;
        if (shorts_ & (1<<0)) do_start(); // READY_START
        fire_irq();
    }

    void do_rxen() {
        state_ = RXRU;
        state_ = RXIDLE;
        ev_ready_ = 1;
        if (shorts_ & (1<<0)) do_start(); // READY_START
        fire_irq();
    }

    void do_start() {
        if (state_ == TXIDLE) {
            state_ = TX;
            ev_address_ = 1;
            ev_payload_ = 1;
            ev_end_ = 1;
            state_ = TXIDLE; // Back to idle after instant TX
            if (shorts_ & (1<<1)) do_disable(); // END_DISABLE
            else fire_irq();
        } else if (state_ == RXIDLE) {
            state_ = RX; // Stay in RX, no packets arrive
        }
    }

    void do_stop() {
        if (state_ == RX) state_ = RXIDLE;
        else if (state_ == TX) state_ = TXIDLE;
        // Then auto-disable
        do_disable();
    }

    bool in_disable_ = false;

    void do_disable() {
        if (in_disable_) return; // Prevent re-entrant loop
        in_disable_ = true;
        if (state_ >= TXRU) state_ = TXDISABLE;
        else if (state_ >= RXRU) state_ = RXDISABLE;
        state_ = DISABLED;
        ev_disabled_ = 1;
        fire_irq();
        // DISABLED shorts for TX/RX chaining (NimBLE uses these)
        if (shorts_ & (1<<2)) do_txen();       // DISABLED_TXEN
        else if (shorts_ & (1<<3)) do_rxen();   // DISABLED_RXEN
        in_disable_ = false;
    }
};

} // namespace pt
