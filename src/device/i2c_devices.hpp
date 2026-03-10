#pragma once
#include "twim.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

namespace pt {

// CST816S Touch Controller (I2C addr 0x15)
class CST816S : public I2CDevice {
    uint8_t regs_[256]{};
    uint8_t reg_ptr_ = 0;
    bool touched_ = false;
    uint16_t touch_x_ = 0, touch_y_ = 0;
    uint8_t gesture_ = 0;

    std::function<void(bool)> irq_cb_;

public:
    static constexpr uint8_t I2C_ADDR = 0x15;

    void set_irq_callback(std::function<void(bool)> cb) { irq_cb_ = cb; }

    void set_touch(bool touched, uint16_t x, uint16_t y, uint8_t gesture = 0) {
        touched_ = touched;
        touch_x_ = x;
        touch_y_ = y;
        gesture_ = gesture;
        update_regs();
        if (irq_cb_) irq_cb_(touched);
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
        uint8_t action = touched_ ? 0 : 1;        // 0=Down, 1=Up
        regs_[0x01] = gesture_;                    // GestureID
        regs_[0x02] = touched_ ? 1 : 0;           // FingerNum
        regs_[0x03] = (action << 6) | ((touch_x_ >> 8) & 0x0F);
        regs_[0x04] = touch_x_ & 0xFF;
        regs_[0x05] = (touch_y_ >> 8) & 0x0F;
        regs_[0x06] = touch_y_ & 0xFF;
        regs_[0xA7] = 0xB4;                        // ChipID
    }
};

// HRS3300 Heart Rate Sensor (I2C addr 0x44)
// Generates synthetic PPG waveform at 72 BPM when enabled.
// InfiniTime reads via ReadHrs():
//   m=reg[0x09], h=reg[0x08], l=reg[0x0A]
//   value = (m<<8) | ((h&0xF)<<4) | (l&0xF) | ((l&0x30)<<12)
class HRS3300 : public I2CDevice {
    uint8_t regs_[256]{};
    uint8_t reg_ptr_ = 0;
    const uint64_t* cycles_ = nullptr;

public:
    static constexpr uint8_t I2C_ADDR = 0x44;
    uint32_t read_count_ = 0;
    uint32_t write_count_ = 0;
    uint8_t reg(uint8_t addr) const { return regs_[addr]; }

    HRS3300() {
        regs_[0x00] = 0x21; // ID register
        regs_[0x01] = 0x00; // Enable (off by default)
    }

    void set_cycles(const uint64_t* c) { cycles_ = c; }

    void i2c_write(const uint8_t* data, uint32_t len) override {
        if (len >= 1) reg_ptr_ = data[0];
        if (len >= 2) {
            regs_[reg_ptr_] = data[1];
            write_count_++;
        }
    }

    void i2c_read(uint8_t* data, uint32_t len) override {
        read_count_++;
        if (cycles_) update_ppg();
        for (uint32_t i = 0; i < len; i++)
            data[i] = regs_[(uint8_t)(reg_ptr_ + i)];
    }

private:
    // Simple hash for deterministic noise
    static uint32_t noise(uint64_t x) {
        x ^= x >> 17; x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 31; x *= 0x94d049bb133111ebULL;
        return (uint32_t)(x >> 32);
    }

    void update_ppg() {
        double t = *cycles_ / 64e6;
        constexpr double bpm = 72.0;
        double phase = 2.0 * M_PI * (bpm / 60.0) * t;

        // PPG waveform: sharp systolic peak + slower diastolic decay
        double systolic = exp(-0.5 * pow(fmod(phase, 2*M_PI) - 1.0, 2) / 0.15);
        double diastolic = 0.3 * exp(-0.5 * pow(fmod(phase, 2*M_PI) - 2.0, 2) / 0.3);
        double ppg_ac = systolic + diastolic;

        // Noise: deterministic from cycle count, ~2% amplitude
        uint32_t n = noise(*cycles_ / 640000); // changes every ~10ms
        double jitter = ((int32_t)(n & 0xFFF) - 2048) / 2048.0; // [-1, 1]

        double ppg = 30000.0 + 10000.0 * ppg_ac + 600.0 * jitter;
        uint32_t v = (uint32_t)std::max(0.0, std::min(ppg, 262143.0));

        // Encode for InfiniTime's ReadHrs():
        //   m=reg[0x09], h=reg[0x0A], l=reg[0x0F]
        //   value = (m<<8) | ((h&0xF)<<4) | (l&0xF) | ((l&0x30)<<12)
        regs_[0x09] = (v >> 8) & 0xFF;                          // m: bits [15:8]
        regs_[0x0A] = (v >> 4) & 0x0F;                          // h: bits [7:4]
        regs_[0x0F] = (v & 0x0F) | (((v >> 16) & 0x03) << 4);  // l: bits [3:0] + [17:16]

        // ALS via ReadAls(): m=reg[0x08], h=reg[0x0D], l=reg[0x0E]
        //   value = (m<<3) | ((h&0x3F)<<11) | (l&0x07)
        // Low constant (indoor, sensor on wrist) ≈ 100
        regs_[0x08] = 0x0C;  // m: 0x0C << 3 = 96
        regs_[0x0D] = 0x00;  // h: 0
        regs_[0x0E] = 0x04;  // l: 4 → total ALS ≈ 100
    }
};

// BMA421/BMA425 Accelerometer (I2C addr 0x18)
// Simulates walking motion on accel axes + incrementing step counter.
// Step counter at 0x1E-0x21 (32-bit LE), ~100 steps/min.
class BMA4xx : public I2CDevice {
    uint8_t regs_[256]{};
    uint8_t reg_ptr_ = 0;
    const uint64_t* cycles_ = nullptr;

public:
    static constexpr uint8_t I2C_ADDR = 0x18;

    BMA4xx() {
        regs_[0x00] = 0x11; // Chip ID (BMA421)
        regs_[0x02] = 0x00; // ERR_REG
        regs_[0x03] = 0x01; // STATUS (data ready)
        // Default: standing still (Z = +1g)
        regs_[0x16] = 0x00; regs_[0x17] = 0x10; // ACC_Z ~4096
        regs_[0x2A] = 0x01; // INTERNAL_STATUS (BMA4_INTERNAL_STAT): init OK
    }

    void set_cycles(const uint64_t* c) { cycles_ = c; }

    void set_accel(int16_t x, int16_t y, int16_t z) {
        regs_[0x12] = x & 0xFF; regs_[0x13] = (x >> 8) & 0xFF;
        regs_[0x14] = y & 0xFF; regs_[0x15] = (y >> 8) & 0xFF;
        regs_[0x16] = z & 0xFF; regs_[0x17] = (z >> 8) & 0xFF;
    }

    void i2c_write(const uint8_t* data, uint32_t len) override {
        if (len >= 1) reg_ptr_ = data[0];
        for (uint32_t i = 1; i < len; i++)
            regs_[(uint8_t)(reg_ptr_ + i - 1)] = data[i];
    }

    void i2c_read(uint8_t* data, uint32_t len) override {
        if (cycles_) update_motion();
        for (uint32_t i = 0; i < len; i++)
            data[i] = regs_[(uint8_t)(reg_ptr_ + i)];
    }

private:
    void update_motion() {
        double t = *cycles_ / 64e6;

        // Step counter: ~100 steps/min = 1.667/sec
        uint32_t steps = (uint32_t)(t * (100.0 / 60.0));
        regs_[0x1E] = steps & 0xFF;
        regs_[0x1F] = (steps >> 8) & 0xFF;
        regs_[0x20] = (steps >> 16) & 0xFF;
        regs_[0x21] = (steps >> 24) & 0xFF;

        // Walking acceleration: periodic motion at step frequency ~1.7 Hz
        // LSB = 4096/g at ±4g range
        constexpr double step_freq = 1.7;
        double phase = 2.0 * M_PI * step_freq * t;
        int16_t ax = (int16_t)(400.0 * sin(phase + 1.0));       // lateral sway ~0.1g
        int16_t ay = (int16_t)(600.0 * sin(phase + 2.1));       // fore-aft ~0.15g
        int16_t az = (int16_t)(4096.0 + 1200.0 * sin(phase));   // vertical: 1g + bounce ~0.3g
        set_accel(ax, ay, az);
    }
};

} // namespace pt
