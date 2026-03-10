#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace pt {

// I2C device interface: address-based routing
struct I2CDevice {
    virtual ~I2CDevice() = default;
    // Write: data sent from master to device
    virtual void i2c_write(const uint8_t* data, uint32_t len) = 0;
    // Read: device sends data to master
    virtual void i2c_read(uint8_t* data, uint32_t len) = 0;
};

// nRF52 TWIM (I2C Master with EasyDMA)
// TWIM0: 0x40003000 (shared with SPIM0), TWIM1: 0x40004000 (shared with SPIM1)
class TWIM : public Device {
    uint32_t txd_ptr_ = 0;
    uint32_t txd_maxcnt_ = 0;
    uint32_t txd_amount_ = 0;
    uint32_t rxd_ptr_ = 0;
    uint32_t rxd_maxcnt_ = 0;
    uint32_t rxd_amount_ = 0;

    uint32_t address_ = 0;
    uint32_t frequency_ = 0;
    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;
    uint32_t enable_ = 0;
    uint32_t errorsrc_ = 0;

    uint32_t scl_pin_ = 0xFFFFFFFF;
    uint32_t sda_pin_ = 0xFFFFFFFF;

    uint32_t ev_stopped_ = 0;
    uint32_t ev_error_ = 0;
    uint32_t ev_suspended_ = 0;
    uint32_t ev_rxstarted_ = 0;
    uint32_t ev_txstarted_ = 0;
    uint32_t ev_lastrx_ = 0;
    uint32_t ev_lasttx_ = 0;

    Bus* bus_ = nullptr;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    std::unordered_map<uint8_t, I2CDevice*> devices_;

public:
    void set_bus(Bus* bus) { bus_ = bus; }
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }
    void attach(uint8_t addr, I2CDevice* dev) { devices_[addr] = dev; }

    uint32_t read(uint32_t addr, Width /*w*/) override {
        switch (addr) {
        case 0x104: return ev_stopped_;
        case 0x124: return ev_error_;
        case 0x148: return ev_suspended_;
        case 0x14C: return ev_rxstarted_;
        case 0x150: return ev_txstarted_;
        case 0x15C: return ev_lastrx_;
        case 0x160: return ev_lasttx_;
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;
        case 0x4C4: return errorsrc_;
        case 0x500: return enable_;
        case 0x508: return scl_pin_;
        case 0x50C: return sda_pin_;
        case 0x524: return frequency_;
        case 0x534: return rxd_ptr_;
        case 0x538: return rxd_maxcnt_;
        case 0x53C: return rxd_amount_;
        case 0x544: return txd_ptr_;
        case 0x548: return txd_maxcnt_;
        case 0x54C: return txd_amount_;
        case 0x588: return address_;
        }
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        switch (addr) {
        case 0x000: // TASKS_STARTRX
            if (val) do_rx();
            break;
        case 0x008: // TASKS_STARTTX
            if (val) do_tx();
            break;
        case 0x014: // TASKS_STOP
            if (val) { ev_stopped_ = 1; update_irq(); }
            break;
        case 0x01C: // TASKS_SUSPEND
            if (val) { ev_suspended_ = 1; }
            break;
        case 0x020: // TASKS_RESUME
            break;
        case 0x104: ev_stopped_ = val; break;
        case 0x124: ev_error_ = val; break;
        case 0x148: ev_suspended_ = val; break;
        case 0x14C: ev_rxstarted_ = val; break;
        case 0x150: ev_txstarted_ = val; break;
        case 0x15C: ev_lastrx_ = val; break;
        case 0x160: ev_lasttx_ = val; break;
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x4C4: errorsrc_ &= ~val; break; // Write 1 to clear
        case 0x500: enable_ = val; break;
        case 0x508: scl_pin_ = val; break;
        case 0x50C: sda_pin_ = val; break;
        case 0x524: frequency_ = val; break;
        case 0x534: rxd_ptr_ = val; break;
        case 0x538: rxd_maxcnt_ = val; break;
        case 0x544: txd_ptr_ = val; break;
        case 0x548: txd_maxcnt_ = val; break;
        case 0x588: address_ = val & 0x7F; break;
        }
    }

private:
    void do_tx() {
        if (!bus_) return;
        ev_txstarted_ = 1;

        auto it = devices_.find(address_);
        if (it != devices_.end()) {
            uint8_t buf[256];
            uint32_t len = txd_maxcnt_ > 256 ? 256 : txd_maxcnt_;
            for (uint32_t i = 0; i < len; i++)
                buf[i] = bus_->read8(txd_ptr_ + i);
            it->second->i2c_write(buf, len);
            txd_amount_ = len;
        } else {
            txd_amount_ = txd_maxcnt_;
        }

        ev_lasttx_ = 1;

        // SHORTS: LASTTX_STOP
        if (shorts_ & (1u << 9)) {
            ev_stopped_ = 1;
        }
        // SHORTS: LASTTX_SUSPEND
        if (shorts_ & (1u << 8)) {
            ev_suspended_ = 1;
        }
        // SHORTS: LASTTX_STARTRX
        if (shorts_ & (1u << 7)) {
            do_rx();
        }

        update_irq();
    }

    void do_rx() {
        if (!bus_) return;
        ev_rxstarted_ = 1;

        auto it = devices_.find(address_);
        uint8_t buf[256];
        uint32_t len = rxd_maxcnt_ > 256 ? 256 : rxd_maxcnt_;

        if (it != devices_.end()) {
            it->second->i2c_read(buf, len);
        } else {
            memset(buf, 0xFF, len);
        }

        for (uint32_t i = 0; i < len; i++)
            bus_->write8(rxd_ptr_ + i, buf[i]);
        rxd_amount_ = len;

        ev_lastrx_ = 1;

        // SHORTS: LASTRX_STOP
        if (shorts_ & (1u << 12)) {
            ev_stopped_ = 1;
        }

        update_irq();
    }

    void update_irq() {
        if (!nvic_) return;
        bool pending = false;
        pending |= (inten_ & (1 << 1)) && ev_stopped_;
        pending |= (inten_ & (1 << 9)) && ev_error_;
        pending |= (inten_ & (1 << 18)) && ev_suspended_;
        pending |= (inten_ & (1 << 19)) && ev_rxstarted_;
        pending |= (inten_ & (1 << 20)) && ev_txstarted_;
        pending |= (inten_ & (1 << 23)) && ev_lastrx_;
        pending |= (inten_ & (1 << 24)) && ev_lasttx_;
        if (pending) nvic_->set_pending(irq_);
    }
};

} // namespace pt
