#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include <array>
#include <cstdio>

namespace pt {

// nRF52 TIMER peripheral (32-bit timer/counter with capture/compare)
// TIMER0: 0x40008000, TIMER1: 0x40009000, TIMER2: 0x4000A000
// TIMER3: 0x4001A000, TIMER4: 0x4001B000
class Timer : public Device {
    bool running_ = false;
    uint32_t mode_ = 0;        // 0=Timer, 1=Counter, 2=LowPowerCounter
    uint32_t bitmode_ = 0;     // 0=16bit, 1=8bit, 2=24bit, 3=32bit
    uint32_t prescaler_ = 0;   // Timer freq = 16MHz / 2^prescaler
    uint32_t counter_ = 0;
    std::array<uint32_t, 6> cc_{};    // Capture/Compare
    std::array<uint32_t, 6> ev_compare_{};
    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;

    uint64_t last_tick_ = 0;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

    uint32_t bitmask() const {
        switch (bitmode_) {
        case 0: return 0xFFFF;     // 16-bit
        case 1: return 0xFF;       // 8-bit
        case 2: return 0xFFFFFF;   // 24-bit
        case 3: return 0xFFFFFFFF; // 32-bit
        }
        return 0xFFFFFFFF;
    }

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    uint32_t read(uint32_t addr, Width /*w*/) override {
        // EVENTS_COMPARE[0..5]
        if (addr >= 0x140 && addr < 0x158) return ev_compare_[(addr - 0x140) / 4];
        // CC[0..5]
        if (addr >= 0x540 && addr < 0x558) return cc_[(addr - 0x540) / 4];

        switch (addr) {
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;
        case 0x504: return mode_;
        case 0x508: return bitmode_;
        case 0x510: return prescaler_;
        }
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        // TASKS
        switch (addr) {
        case 0x000: if (val) running_ = true; return;   // START
        case 0x004: if (val) running_ = false; return;  // STOP
        case 0x008: if (val && mode_ == 1) counter_ = (counter_ + 1) & bitmask(); return; // COUNT
        case 0x00C: if (val) counter_ = 0; return;      // CLEAR
        }
        // TASKS_CAPTURE[0..5]
        if (addr >= 0x040 && addr < 0x058) {
            if (val) cc_[(addr - 0x040) / 4] = counter_ & bitmask();
            return;
        }
        // EVENTS_COMPARE
        if (addr >= 0x140 && addr < 0x158) { ev_compare_[(addr - 0x140) / 4] = val; return; }
        // CC
        if (addr >= 0x540 && addr < 0x558) { cc_[(addr - 0x540) / 4] = val; return; }

        switch (addr) {
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x504: mode_ = val & 3; break;
        case 0x508: bitmode_ = val & 3; break;
        case 0x510: prescaler_ = val & 0xF; break;
        }
    }

    std::optional<Interrupt> tick(uint64_t cycles) override {
        if (!running_ || mode_ != 0) return {};

        // Timer increments at 16MHz / 2^prescaler
        // CPU runs at 64MHz, so timer ticks every 4 * 2^prescaler CPU cycles
        uint64_t divisor = 4ULL << prescaler_;
        uint64_t elapsed = cycles - last_tick_;
        if (elapsed < divisor) return {};
        uint64_t ticks = elapsed / divisor;
        last_tick_ = cycles - (elapsed % divisor);

        uint32_t mask = bitmask();
        bool fired = false;

        for (uint64_t t = 0; t < ticks; t++) {
            counter_ = (counter_ + 1) & mask;
            for (int i = 0; i < 6; i++) {
                if (counter_ == cc_[i]) {
                    ev_compare_[i] = 1;
                    if (shorts_ & (1u << i)) counter_ = 0;       // COMPARE_CLEAR
                    if (shorts_ & (1u << (i + 8))) running_ = false; // COMPARE_STOP
                    if (inten_ & (1u << (16 + i))) fired = true;
                }
            }
        }

        if (fired && nvic_) {
            nvic_->set_pending(irq_);
            return Interrupt{irq_};
        }
        return {};
    }
};

// nRF52 RTC (Real-Time Counter) — 24-bit, 32.768kHz / (PRESCALER+1)
// RTC0: 0x4000B000, RTC1: 0x40011000, RTC2: 0x40024000
class RTC : public Device {
    bool running_ = false;
    uint32_t counter_ = 0;       // 24-bit
    uint32_t prescaler_ = 0;     // 12-bit
    std::array<uint32_t, 4> cc_{};
    std::array<uint32_t, 4> ev_compare_{};
    uint32_t ev_tick_ = 0;
    uint32_t ev_ovrflw_ = 0;
    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;
    uint32_t evten_ = 0;

    uint64_t last_tick_ = 0;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }

    uint32_t read(uint32_t addr, Width /*w*/) override {
        if (addr >= 0x140 && addr < 0x150) return ev_compare_[(addr - 0x140) / 4];
        if (addr >= 0x540 && addr < 0x550) return cc_[(addr - 0x540) / 4];
        switch (addr) {
        case 0x100: return ev_tick_;
        case 0x104: return ev_ovrflw_;
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;
        case 0x340: return evten_;
        case 0x344: return evten_;
        case 0x348: return evten_;
        case 0x504: return counter_;
        case 0x508: return prescaler_;
        }
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        switch (addr) {
        case 0x000: if (val) running_ = true; return;
        case 0x004: if (val) running_ = false; return;
        case 0x008: if (val) counter_ = 0; return;
        case 0x024: return; // TASKS_TRIGOVRFLW
        }
        if (addr >= 0x140 && addr < 0x150) { ev_compare_[(addr - 0x140) / 4] = val; return; }
        if (addr >= 0x540 && addr < 0x550) { cc_[(addr - 0x540) / 4] = val; return; }
        switch (addr) {
        case 0x100: ev_tick_ = val; break;
        case 0x104: ev_ovrflw_ = val; break;
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304: inten_ |= val; break;
        case 0x308: inten_ &= ~val; break;
        case 0x340: evten_ = val; break;
        case 0x344: evten_ |= val; break;
        case 0x348: evten_ &= ~val; break;
        case 0x508: prescaler_ = val & 0xFFF; break;
        }
    }

    std::optional<Interrupt> tick(uint64_t cycles) override {
        if (!running_) return {};

        // RTC base freq = 32768 Hz, divided by (PRESCALER+1)
        // CPU at 64MHz: tick every 64M / (32768 / (prescaler+1)) = 1953 * (prescaler+1) cycles
        uint64_t divisor = 1953ULL * (prescaler_ + 1);
        uint64_t elapsed = cycles - last_tick_;
        if (elapsed < divisor) return {};
        uint64_t ticks = elapsed / divisor;
        last_tick_ = cycles - (elapsed % divisor);

        bool fired = false;
        for (uint64_t t = 0; t < ticks; t++) {
            counter_ = (counter_ + 1) & 0xFFFFFF;
            ev_tick_ = 1;
            if (counter_ == 0) ev_ovrflw_ = 1;

            for (int i = 0; i < 4; i++) {
                if (counter_ == cc_[i]) {
                    ev_compare_[i] = 1;
                    if (shorts_ & (1u << i)) counter_ = 0;
                    if (inten_ & (1u << (16 + i))) fired = true;
                }
            }
            if (inten_ & 1) fired = true; // TICK interrupt
        }

        if (fired && nvic_) {
            nvic_->set_pending(irq_);
            return Interrupt{irq_};
        }
        return {};
    }
};

} // namespace pt
