#pragma once
#include "twim.hpp"
#include <cstdint>
#include <cstring>
#include <functional>

namespace pt {

// CST816S Touch Controller (I2C addr 0x15)
// Returns touch events based on simulated input (mouse)
class CST816S : public I2CDevice {
    uint8_t regs_[256]{};
    uint8_t reg_ptr_ = 0;
    bool touched_ = false;
    uint16_t touch_x_ = 0, touch_y_ = 0;
    uint8_t gesture_ = 0;

    // Callback to toggle IRQ pin (active low)
    std::function<void(bool)> irq_cb_;

public:
    static constexpr uint8_t I2C_ADDR = 0x15;

    // cb(true) = IRQ asserted (pin LOW), cb(false) = IRQ released (pin HIGH)
    void set_irq_callback(std::function<void(bool)> cb) { irq_cb_ = cb; }

    void set_touch(bool touched, uint16_t x, uint16_t y, uint8_t gesture = 0) {
        touched_ = touched;
        touch_x_ = x;
        touch_y_ = y;
        gesture_ = gesture;
        update_regs();
        if (irq_cb_) irq_cb_(touched); // Assert IRQ on touch, release on lift
    }

    void i2c_write(const uint8_t* data, uint32_t len) override {
        if (len >= 1) reg_ptr_ = data[0];
    }

    void i2c_read(uint8_t* data, uint32_t len) override {
        update_regs();
        for (uint32_t i = 0; i < len; i++)
            data[i] = (reg_ptr_ + i < sizeof(regs_)) ? regs_[reg_ptr_ + i] : 0;
    }

private:
    void update_regs() {
        memset(regs_, 0, sizeof(regs_));
        regs_[0x01] = gesture_;                    // GestureID
        regs_[0x02] = touched_ ? 1 : 0;           // FingerNum
        regs_[0x03] = (touch_x_ >> 8) & 0x0F;     // XposH (event + X high)
        regs_[0x04] = touch_x_ & 0xFF;             // XposL
        regs_[0x05] = (touch_y_ >> 8) & 0x0F;     // YposH
        regs_[0x06] = touch_y_ & 0xFF;             // YposL
        regs_[0xA7] = 0xB4;                        // ChipID (CST816S)
    }
};

// HRS3300 Heart Rate Sensor (I2C addr 0x44)
class HRS3300 : public I2CDevice {
    uint8_t regs_[256]{};
    uint8_t reg_ptr_ = 0;

public:
    static constexpr uint8_t I2C_ADDR = 0x44;

    HRS3300() {
        regs_[0x00] = 0x21; // ID register
        regs_[0x01] = 0x00; // Enable (off by default)
    }

    void i2c_write(const uint8_t* data, uint32_t len) override {
        if (len >= 1) reg_ptr_ = data[0];
        if (len >= 2) regs_[reg_ptr_] = data[1];
    }

    void i2c_read(uint8_t* data, uint32_t len) override {
        for (uint32_t i = 0; i < len; i++)
            data[i] = regs_[(uint8_t)(reg_ptr_ + i)];
    }
};

// BMA421/BMA425 Accelerometer (I2C addr 0x18)
class BMA4xx : public I2CDevice {
    uint8_t regs_[256]{};
    uint8_t reg_ptr_ = 0;

public:
    static constexpr uint8_t I2C_ADDR = 0x18;

    BMA4xx() {
        regs_[0x00] = 0x11; // Chip ID (BMA421)
        regs_[0x02] = 0x00; // ERR_REG
        regs_[0x03] = 0x01; // STATUS (data ready)
        // Accel data (X=0, Y=0, Z=+1g in 12-bit, ~4096)
        regs_[0x12] = 0x00; regs_[0x13] = 0x00; // ACC_X
        regs_[0x14] = 0x00; regs_[0x15] = 0x00; // ACC_Y
        regs_[0x16] = 0x00; regs_[0x17] = 0x10; // ACC_Z (~4096)
        regs_[0x1A] = 0x01; // INTERNAL_STATUS: config stream loaded
        regs_[0x2A] = 0x01; // STEP counter (low)
    }

    void set_accel(int16_t x, int16_t y, int16_t z) {
        regs_[0x12] = x & 0xFF; regs_[0x13] = (x >> 8) & 0xFF;
        regs_[0x14] = y & 0xFF; regs_[0x15] = (y >> 8) & 0xFF;
        regs_[0x16] = z & 0xFF; regs_[0x17] = (z >> 8) & 0xFF;
    }

    void i2c_write(const uint8_t* data, uint32_t len) override {
        if (len >= 1) reg_ptr_ = data[0];
        // Multi-byte write
        for (uint32_t i = 1; i < len; i++)
            regs_[(uint8_t)(reg_ptr_ + i - 1)] = data[i];
    }

    void i2c_read(uint8_t* data, uint32_t len) override {
        for (uint32_t i = 0; i < len; i++)
            data[i] = regs_[(uint8_t)(reg_ptr_ + i)];
    }
};

} // namespace pt
