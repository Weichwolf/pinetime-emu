#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <cstdio>
#include <cstring>
#include <functional>

namespace pt {

// nRF52 UARTE (UART with EasyDMA) at 0x40002000
// Captures TX output for debug/logging. RX is stubbed.
class UARTE : public Device {
    // Events
    uint32_t ev_cts_ = 0, ev_ncts_ = 0;
    uint32_t ev_rxdrdy_ = 0, ev_endrx_ = 0, ev_rxto_ = 0;
    uint32_t ev_txdrdy_ = 0, ev_endtx_ = 0, ev_txstarted_ = 0;
    uint32_t ev_error_ = 0, ev_rxstarted_ = 0;
    // Config
    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;
    uint32_t errorsrc_ = 0;
    uint32_t enable_ = 0;       // 0=disabled, 8=enabled
    uint32_t baudrate_ = 0x01D7E000; // 115200
    uint32_t config_ = 0;
    // Pin select (ignored but stored)
    uint32_t psel_rts_ = 0xFFFFFFFF;
    uint32_t psel_txd_ = 0xFFFFFFFF;
    uint32_t psel_cts_ = 0xFFFFFFFF;
    uint32_t psel_rxd_ = 0xFFFFFFFF;
    // DMA pointers
    uint32_t rxd_ptr_ = 0, rxd_maxcnt_ = 0, rxd_amount_ = 0;
    uint32_t txd_ptr_ = 0, txd_maxcnt_ = 0, txd_amount_ = 0;

    Bus* bus_ = nullptr;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    // TX output callback
    std::function<void(const char*, size_t)> tx_callback_;

    void fire_irq() {
        if (nvic_) nvic_->set_pending(irq_);
    }

public:
    void set_bus(Bus* b) { bus_ = b; }
    void set_nvic(NVIC* n, uint32_t irq) { nvic_ = n; irq_ = irq; }

    // Set callback for TX output (e.g. to write to stderr or file)
    void set_tx_callback(std::function<void(const char*, size_t)> cb) { tx_callback_ = cb; }

    uint32_t read(uint32_t addr, Width) override {
        switch (addr) {
        case 0x100: return ev_cts_;
        case 0x104: return ev_ncts_;
        case 0x108: return ev_rxdrdy_;
        case 0x110: return ev_endrx_;
        case 0x120: return ev_endtx_;
        case 0x124: return ev_error_;
        case 0x144: return ev_rxto_;
        case 0x148: return ev_rxstarted_;
        case 0x150: return ev_txstarted_;
        case 0x158: return ev_txdrdy_;
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;
        case 0x480: return errorsrc_;
        case 0x500: return enable_;
        case 0x508: return psel_rts_;
        case 0x50C: return psel_txd_;
        case 0x510: return psel_cts_;
        case 0x514: return psel_rxd_;
        case 0x524: return baudrate_;
        case 0x534: return rxd_ptr_;
        case 0x538: return rxd_maxcnt_;
        case 0x53C: return rxd_amount_;
        case 0x544: return txd_ptr_;
        case 0x548: return txd_maxcnt_;
        case 0x54C: return txd_amount_;
        case 0x56C: return config_;
        }
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        switch (addr) {
        // Tasks
        case 0x000: // STARTRX
            if (val && enable_ == 8) {
                ev_rxstarted_ = 1;
                if (inten_ & (1u << 19)) fire_irq();
            }
            break;
        case 0x004: // STOPRX
            if (val) {
                ev_rxto_ = 1;
                ev_endrx_ = 1;
                if (inten_ & (1u << 17)) fire_irq();
            }
            break;
        case 0x008: // STARTTX
            if (val && enable_ == 8) do_tx();
            break;
        case 0x00C: // STOPTX
            break;
        case 0x02C: // FLUSHRX
            break;
        // Events
        case 0x100: ev_cts_ = val; break;
        case 0x104: ev_ncts_ = val; break;
        case 0x108: ev_rxdrdy_ = val; break;
        case 0x110: ev_endrx_ = val; break;
        case 0x120: ev_endtx_ = val; break;
        case 0x124: ev_error_ = val; break;
        case 0x144: ev_rxto_ = val; break;
        case 0x148: ev_rxstarted_ = val; break;
        case 0x150: ev_txstarted_ = val; break;
        case 0x158: ev_txdrdy_ = val; break;
        // Config
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x480: errorsrc_ &= ~val; break; // W1C
        case 0x500: enable_ = val & 0xF; break;
        case 0x508: psel_rts_ = val; break;
        case 0x50C: psel_txd_ = val; break;
        case 0x510: psel_cts_ = val; break;
        case 0x514: psel_rxd_ = val; break;
        case 0x524: baudrate_ = val; break;
        case 0x534: rxd_ptr_ = val; break;
        case 0x538: rxd_maxcnt_ = val; break;
        case 0x544: txd_ptr_ = val; break;
        case 0x548: txd_maxcnt_ = val; break;
        case 0x56C: config_ = val; break;
        }
    }

private:
    void do_tx() {
        if (!bus_ || txd_maxcnt_ == 0) return;

        ev_txstarted_ = 1;
        if (inten_ & (1u << 20)) fire_irq();

        // Read TX data from DMA buffer
        char buf[256];
        uint32_t len = txd_maxcnt_ > sizeof(buf) ? sizeof(buf) : txd_maxcnt_;
        for (uint32_t i = 0; i < len; i++)
            buf[i] = bus_->read8(txd_ptr_ + i);

        txd_amount_ = len;

        // Output
        if (tx_callback_) {
            tx_callback_(buf, len);
        } else {
            fwrite(buf, 1, len, stderr);
        }

        // Fire completion events
        ev_txdrdy_ = 1;
        ev_endtx_ = 1;
        if (inten_ & (1u << 7)) fire_irq();  // TXDRDY
        if (inten_ & (1u << 8)) fire_irq();  // ENDTX
    }
};

} // namespace pt
