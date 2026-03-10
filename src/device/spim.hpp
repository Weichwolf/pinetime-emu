#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace pt {

// SPI device callback: receives TX data, returns RX data
// Called when a SPIM transfer completes
using SpiCallback = std::function<void(const uint8_t* tx, uint32_t tx_len,
                                        uint8_t* rx, uint32_t rx_len)>;

// nRF52 SPIM (SPI Master with EasyDMA)
// SPIM0: 0x40003000, SPIM1: 0x40004000, SPIM2: 0x40023000
class SPIM : public Device {
    // DMA pointers (addresses in nRF52 memory space)
    uint32_t txd_ptr_ = 0;
    uint32_t txd_maxcnt_ = 0;
    uint32_t txd_amount_ = 0;
    uint32_t rxd_ptr_ = 0;
    uint32_t rxd_maxcnt_ = 0;
    uint32_t rxd_amount_ = 0;

    // Config
    uint32_t frequency_ = 0;
    uint32_t config_ = 0;
    uint32_t orc_ = 0xFF;     // Over-read character

    // Pin config (we don't actually route signals, but firmware writes these)
    uint32_t sck_pin_ = 0xFFFFFFFF;
    uint32_t mosi_pin_ = 0xFFFFFFFF;
    uint32_t miso_pin_ = 0xFFFFFFFF;

    // Events (SPIM)
    uint32_t ev_stopped_ = 0;
    uint32_t ev_endrx_ = 0;
    uint32_t ev_end_ = 0;
    uint32_t ev_endtx_ = 0;
    uint32_t ev_started_ = 0;

    // Legacy SPI mode with RX FIFO (pipelined transfers need buffering)
    uint32_t ev_ready_count_ = 0;
    uint8_t  legacy_txd_ = 0;
    static constexpr int RX_FIFO_SIZE = 8;
    uint8_t  rx_fifo_[RX_FIFO_SIZE]{};
    int rx_fifo_head_ = 0;  // write position
    int rx_fifo_tail_ = 0;  // read position
    int rx_fifo_count_ = 0;

    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;
    uint32_t enable_ = 0;

    Bus* bus_ = nullptr;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;
    SpiCallback callback_;

public:
    void set_bus(Bus* bus) { bus_ = bus; }
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }
    void set_callback(SpiCallback cb) { callback_ = cb; }

    uint32_t read(uint32_t addr, Width /*w*/) override {
        switch (addr) {
        // Legacy SPI
        case 0x108: return ev_ready_count_ > 0 ? 1 : 0;
        case 0x518: { // RXD: read from FIFO
            if (rx_fifo_count_ > 0) {
                uint8_t val = rx_fifo_[rx_fifo_tail_];
                rx_fifo_tail_ = (rx_fifo_tail_ + 1) % RX_FIFO_SIZE;
                rx_fifo_count_--;
                return val;
            }
            return 0xFF;
        }
        // SPIM events
        case 0x104: return ev_stopped_;
        case 0x110: return ev_endrx_;
        case 0x118: return ev_end_;
        case 0x120: return ev_endtx_;
        case 0x14C: return ev_started_;
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;
        case 0x500: return enable_;
        case 0x508: return sck_pin_;
        case 0x50C: return mosi_pin_;
        case 0x510: return miso_pin_;
        case 0x524: return frequency_;
        case 0x534: return rxd_ptr_;
        case 0x538: return rxd_maxcnt_;
        case 0x53C: return rxd_amount_;
        case 0x544: return txd_ptr_;
        case 0x548: return txd_maxcnt_;
        case 0x54C: return txd_amount_;
        case 0x554: return config_;
        case 0x5C0: return orc_;
        }
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        switch (addr) {
        case 0x010: // TASKS_START
            if (val) {
                do_transfer();
            }
            break;
        case 0x014: // TASKS_STOP
            if (val) { ev_stopped_ = 1; fire_irq(1); }
            break;
        case 0x018: // TASKS_SUSPEND
            break;
        case 0x01C: // TASKS_RESUME
            break;
        // Legacy SPI: write 0 to clear one pending READY
        case 0x108: if (val == 0 && ev_ready_count_ > 0) ev_ready_count_--; break;
        case 0x51C: // TXD: instant transfer
            legacy_txd_ = val & 0xFF;
            do_legacy_transfer();
            break;
        // SPIM events
        case 0x104: ev_stopped_ = val; break;
        case 0x110: ev_endrx_ = val; break;
        case 0x118: ev_end_ = val; break;
        case 0x120: ev_endtx_ = val; break;
        case 0x14C: ev_started_ = val; break;
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x500: enable_ = val; break;
        case 0x508: sck_pin_ = val; break;
        case 0x50C: mosi_pin_ = val; break;
        case 0x510: miso_pin_ = val; break;
        case 0x524: frequency_ = val; break;
        case 0x534: rxd_ptr_ = val; break;
        case 0x538: rxd_maxcnt_ = val; break;
        case 0x544: txd_ptr_ = val; break;
        case 0x548: txd_maxcnt_ = val; break;
        case 0x554: config_ = val; break;
        case 0x5C0: orc_ = val & 0xFF; break;
        }
    }

private:
    void do_legacy_transfer() {
        uint8_t rx = 0xFF;
        if (callback_) callback_(&legacy_txd_, 1, &rx, 1);
        // Push RX byte into FIFO
        if (rx_fifo_count_ < RX_FIFO_SIZE) {
            rx_fifo_[rx_fifo_head_] = rx;
            rx_fifo_head_ = (rx_fifo_head_ + 1) % RX_FIFO_SIZE;
            rx_fifo_count_++;
        }
        ev_ready_count_++;
    }

    uint32_t transfer_count_ = 0;
public:
    uint32_t transfer_count() const { return transfer_count_; }
private:
    void do_transfer() {
        if (!bus_ || enable_ != 7) return;

        ev_started_ = 1;

        uint32_t tx_len = txd_maxcnt_;
        uint32_t rx_len = rxd_maxcnt_;

        // Dynamic buffers for DMA transfer (InfiniTime sends up to ~10KB chunks)
        std::vector<uint8_t> tx_buf(tx_len), rx_buf(rx_len, orc_);

        for (uint32_t i = 0; i < tx_len; i++)
            tx_buf[i] = bus_->read8(txd_ptr_ + i);

        if (callback_) callback_(tx_buf.data(), tx_len, rx_buf.data(), rx_len);

        for (uint32_t i = 0; i < rx_len; i++)
            bus_->write8(rxd_ptr_ + i, rx_buf[i]);

        txd_amount_ = tx_len;
        rxd_amount_ = rx_len;
        ev_endtx_ = 1;
        ev_endrx_ = 1;
        ev_end_ = 1;
        transfer_count_++;

        fire_irq(6);  // END event = bit 6
        fire_irq(4);  // ENDTX event = bit 4  (for INTENSET mapping)

        // SHORTS: END_START
        if (shorts_ & (1u << 17)) do_transfer(); // recurse (rare)
    }

    void fire_irq(uint32_t /*event_bit*/) {
        // Simplified: fire if any interrupt enabled and any event set
        if (!nvic_) return;
        bool pending = (inten_ & (1 << 1)) && ev_stopped_;
        pending |= (inten_ & (1 << 4)) && ev_endrx_;
        pending |= (inten_ & (1 << 6)) && ev_end_;
        pending |= (inten_ & (1 << 8)) && ev_endtx_;
        pending |= (inten_ & (1 << 19)) && ev_started_;
        if (pending) nvic_->set_pending(irq_);
    }
};

} // namespace pt
