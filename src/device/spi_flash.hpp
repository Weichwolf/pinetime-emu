#pragma once
#include "../bus.hpp"
#include <cstring>
#include <vector>

namespace pt {

// External SPI NOR Flash (XT25F32B, 4MB)
// Directly driven by SPIM via callback, not bus-mapped as peripheral.
// Memory-mapped view at 0x04000000 for MCUBoot/InfiniTime read access.
class SpiFlash : public Device {
    std::vector<uint8_t> data_;

    // SPI command state machine
    uint8_t cmd_ = 0;
    uint32_t addr_ = 0;
    uint8_t phase_ = 0;    // 0=cmd, 1-3=addr bytes, 4+=data
    bool wren_ = false;

public:
    uint32_t read_count_ = 0, write_count_ = 0, status_count_ = 0;
    explicit SpiFlash(uint32_t size) : data_(size, 0xFF) {}

    uint8_t* data() { return data_.data(); }
    uint32_t size() const { return data_.size(); }
    uint8_t cmd() const { return cmd_; }
    uint8_t phase() const { return phase_; }

    void reset_cmd() { cmd_ = 0; phase_ = 0; }

    // SPI transfer (called from SPIM callback)
    // Handles split TX/RX transfers (firmware sends cmd in TX-only, then reads in RX-only)
    void transfer(const uint8_t* tx, uint32_t tx_len,
                  uint8_t* rx, uint32_t rx_len) {
        if (tx_len > 1) { dma_transfer(tx, tx_len, rx, rx_len); return; }

        // Process byte-by-byte: max(tx_len, rx_len) clock cycles
        uint32_t total = tx_len > rx_len ? tx_len : rx_len;
        for (uint32_t i = 0; i < total; i++) {
            uint8_t mosi = (i < tx_len) ? tx[i] : 0xFF;
            uint8_t miso = process_byte(mosi);
            if (i < rx_len) rx[i] = miso;
        }
    }

    // Device interface: memory-mapped read/write
    uint32_t read(uint32_t addr, Width w) override {
        if (addr >= data_.size()) return 0xFF;
        switch (w) {
        case Width::Byte: return data_[addr];
        case Width::Half: return data_[addr] | (data_[addr+1] << 8);
        case Width::Word: return data_[addr] | (data_[addr+1] << 8) |
                                 (data_[addr+2] << 16) | (data_[addr+3] << 24);
        }
        return 0xFF;
    }

    void write(uint32_t addr, Width w, uint32_t val) override {
        if (addr >= data_.size()) return;
        switch (w) {
        case Width::Byte: data_[addr] = val; break;
        case Width::Half: data_[addr] = val; data_[addr+1] = val >> 8; break;
        case Width::Word:
            data_[addr] = val; data_[addr+1] = val >> 8;
            data_[addr+2] = val >> 16; data_[addr+3] = val >> 24; break;
        }
    }

    uint8_t process_byte(uint8_t tx_byte) {
        uint8_t rx_byte = 0xFF;
        if (phase_ == 0) {
            cmd_ = tx_byte;
            addr_ = 0;
            phase_ = 1;
            switch (cmd_) {
            case 0x06: wren_ = true; phase_ = 0; break;
            case 0x04: wren_ = false; phase_ = 0; break;
            case 0xC7:
                if (wren_) memset(data_.data(), 0xFF, data_.size());
                wren_ = false; phase_ = 0; break;
            case 0xAB: phase_ = 4; break;
            }
        } else if (cmd_ == 0x9F) {
            static const uint8_t jedec[] = {0x0B, 0x40, 0x16};
            rx_byte = (phase_ <= 3) ? jedec[phase_ - 1] : 0xFF;
            phase_++;
        } else if (cmd_ == 0x05) {
            rx_byte = wren_ ? 0x02 : 0x00; // bit1=WEL, bit0=WIP(always 0)
            phase_++;
            status_count_++;
        } else if (phase_ <= 3) {
            addr_ = (addr_ << 8) | tx_byte;
            phase_++;
        } else if (cmd_ == 0x0B && phase_ == 4) {
            phase_++; // Fast read dummy byte
        } else {
            uint32_t fa = addr_;
            switch (cmd_) {
            case 0x03: case 0x0B:
                rx_byte = (fa < data_.size()) ? data_[fa] : 0xFF;
                addr_++; read_count_++; break;
            case 0x02:
                if (fa < data_.size() && wren_) data_[fa] &= tx_byte;
                addr_++; write_count_++; break;
            case 0x20: case 0xD8: {
                if (wren_) {
                    uint32_t len = (cmd_ == 0x20) ? 4096 : 65536;
                    uint32_t base = fa & ~(len - 1);
                    if (base + len <= data_.size())
                        memset(&data_[base], 0xFF, len);
                }
                wren_ = false; phase_ = 0; break;
            }
            case 0xAB: rx_byte = 0x15; phase_++; break;
            }
        }
        return rx_byte;
    }

private:
    void dma_transfer(const uint8_t* tx, uint32_t tx_len,
                      uint8_t* rx, uint32_t rx_len) {
        uint8_t cmd = tx[0];
        switch (cmd) {
        case 0x9F:
            if (rx_len > 1) rx[1] = 0x0B;
            if (rx_len > 2) rx[2] = 0x40;
            if (rx_len > 3) rx[3] = 0x16;
            cmd_ = 0x9F; phase_ = tx_len;
            return; // preserve state for split TX/RX
        case 0x05:
            if (rx_len > 1) rx[1] = wren_ ? 0x02 : 0x00;
            cmd_ = 0x05; phase_ = 1;
            return; // preserve state for split TX/RX
        case 0x03: case 0x0B: {
            if (tx_len < 4) break;
            uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
            uint32_t skip = (cmd == 0x0B) ? 5 : 4;
            uint32_t bytes_read = 0;
            for (uint32_t i = skip; i < rx_len; i++) {
                uint32_t fa = addr + (i - skip);
                rx[i] = (fa < data_.size()) ? data_[fa] : 0xFF;
                bytes_read++;
            }
            read_count_ += bytes_read;
            // Preserve state for split TX/RX transfers (cmd in one, data in next)
            cmd_ = cmd;
            addr_ = addr + (rx_len > skip ? rx_len - skip : 0);
            phase_ = (cmd == 0x0B) ? 5 : 4;
            return; // Don't reset phase_
        }
        case 0x06: wren_ = true; break;
        case 0x04: wren_ = false; break;
        case 0x02: {
            if (tx_len < 5 || !wren_) break;
            uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
            for (uint32_t i = 4; i < tx_len; i++) {
                uint32_t fa = addr + (i - 4);
                if (fa < data_.size()) data_[fa] &= tx[i];
            }
            write_count_ += tx_len - 4;
            wren_ = false;
            phase_ = 0;
            return;
        }
        case 0x20: case 0xD8: {
            if (tx_len < 4 || !wren_) break;
            uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
            uint32_t len = (cmd == 0x20) ? 4096 : 65536;
            addr &= ~(len - 1);
            if (addr + len <= data_.size())
                memset(&data_[addr], 0xFF, len);
            wren_ = false; break;
        }
        case 0xC7:
            if (wren_) memset(data_.data(), 0xFF, data_.size());
            wren_ = false; break;
        }
        phase_ = 0;
    }
};

} // namespace pt
