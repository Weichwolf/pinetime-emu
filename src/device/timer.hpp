#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include "ppi.hpp"
#include <array>

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
    uint64_t* cpu_cy_ = nullptr;
    const uint32_t* cpu_pc_ = nullptr;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;
    PPI* ppi_ = nullptr;
    uint32_t base_addr_ = 0; // For PPI event address computation

public:
    bool is_running() const { return running_; }
    uint32_t counter() const { return counter_; }
    uint32_t start_count_ = 0, stop_count_ = 0, tick_fires_ = 0;
    void set_cycles(uint64_t* cy, const uint32_t* pc = nullptr) { cpu_cy_ = cy; cpu_pc_ = pc; }
    void set_ppi(PPI* ppi, uint32_t base) { ppi_ = ppi; base_addr_ = base; }
private:

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
        switch (addr) {
        case 0x000: // START
            if (val && !running_) {
                running_ = true;
                start_count_++;
                if (cpu_cy_) last_tick_ = *cpu_cy_;
            }
            return;
        case 0x004: // STOP
            if (val && running_) {
                if (cpu_cy_ && mode_ == 0) advance_counter(*cpu_cy_);
                running_ = false;
                stop_count_++;
            }
            return;
        case 0x008: if (val && mode_ == 1) counter_ = (counter_ + 1) & bitmask(); return; // COUNT
        case 0x00C:
            if (val) counter_ = 0;
            return;
        }
        // TASKS_CAPTURE[0..5]
        if (addr >= 0x040 && addr < 0x058) {
            if (val) {
                if (running_ && cpu_cy_ && mode_ == 0) advance_counter(*cpu_cy_);
                int ci = (addr - 0x040) / 4;
                cc_[ci] = counter_ & bitmask();
            }
            return;
        }
        // EVENTS_COMPARE
        if (addr >= 0x140 && addr < 0x158) { ev_compare_[(addr - 0x140) / 4] = val; return; }
        // CC
        if (addr >= 0x540 && addr < 0x558) {
            cc_[(addr - 0x540) / 4] = val;
            return;
        }

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
        bool fired = advance_counter(cycles);
        if (fired && nvic_) {
            nvic_->set_pending(irq_);
            return Interrupt{irq_};
        }
        return {};
    }

    // Advance counter to given cycle, return true if any interrupt-enabled compare fired
    bool advance_counter(uint64_t cycles) {
        uint64_t divisor = 4ULL << prescaler_;
        uint64_t elapsed = cycles - last_tick_;
        if (elapsed < divisor) return false;
        uint64_t ticks = elapsed / divisor;
        last_tick_ = cycles - (elapsed % divisor);

        uint32_t mask = bitmask();
        bool fired = false;

        for (uint64_t t = 0; t < ticks; t++) {
            counter_ = (counter_ + 1) & mask;
            for (int i = 0; i < 6; i++) {
                if (counter_ == cc_[i]) {
                    // PPI only fires on 0→1 transition (edge detection)
                    if (!ev_compare_[i]) {
                        ev_compare_[i] = 1;
                        if (ppi_) ppi_->on_event(base_addr_ + 0x140 + i * 4);
                    }
                    tick_fires_++;
                    if (shorts_ & (1u << i)) counter_ = 0;
                    if (shorts_ & (1u << (i + 8))) running_ = false;
                    if (inten_ & (1u << (16 + i))) fired = true;
                }
            }
        }
        return fired;
    }

    uint64_t next_event(uint64_t now) const override {
        if (!running_ || mode_ != 0) return UINT64_MAX;
        uint64_t divisor = 4ULL << prescaler_;
        uint32_t mask = bitmask();
        uint64_t min_ticks = UINT64_MAX;
        for (int i = 0; i < 6; i++) {
            bool relevant = (inten_ & (1u << (16 + i))) || (shorts_ & (1u << i)) || (shorts_ & (1u << (i + 8)));
            if (!relevant) continue;
            uint32_t target = cc_[i] & mask;
            uint32_t dist = (target - counter_) & mask;
            uint64_t dist64 = (dist == 0) ? (uint64_t)mask + 1 : dist;
            if (dist64 < min_ticks) min_ticks = dist64;
        }
        // PPI routing: advance timer periodically so CC matches trigger PPI events.
        // Without this, timers with PPI but no interrupts/shorts never advance.
        if (ppi_ && min_ticks == UINT64_MAX) {
            // Find nearest unmatched CC for PPI edge detection
            for (int i = 0; i < 6; i++) {
                if (ev_compare_[i] || cc_[i] == 0) continue;
                uint32_t target = cc_[i] & mask;
                uint32_t dist = (target - counter_) & mask;
                uint64_t dist64 = (dist == 0) ? (uint64_t)mask + 1 : dist;
                if (dist64 < min_ticks) min_ticks = dist64;
            }
        }
        if (min_ticks == UINT64_MAX) return UINT64_MAX;
        uint64_t remainder = (now > last_tick_) ? (now - last_tick_) % divisor : 0;
        return now + min_ticks * divisor - remainder;
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
    uint64_t* cpu_cy_ = nullptr;
    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;
    PPI* ppi_ = nullptr;
    uint32_t base_addr_ = 0;

public:
    uint32_t irq_fire_count_ = 0;
    void set_cycles(uint64_t* cy) { cpu_cy_ = cy; }
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }
    void set_ppi(PPI* ppi, uint32_t base) { ppi_ = ppi; base_addr_ = base; }

    // Diagnostics
    bool diag_running() const { return running_; }
    uint32_t diag_counter() const { return counter_; }
    uint32_t diag_prescaler() const { return prescaler_; }
    uint32_t diag_inten() const { return inten_; }
    uint32_t diag_evten() const { return evten_; }
    uint32_t diag_cc(int i) const { return cc_[i]; }

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
        case 0x000:
            if (val && !running_) {
                running_ = true;
                if (cpu_cy_) last_tick_ = *cpu_cy_;
            }
            return;
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
            if (counter_ == 0) {
                ev_ovrflw_ = 1;
                if (inten_ & 2) fired = true; // OVRFLW interrupt
            }

            for (int i = 0; i < 4; i++) {
                if (counter_ == cc_[i]) {
                    ev_compare_[i] = 1;
                    if (ppi_ && (evten_ & (1u << (16 + i))))
                        ppi_->on_event(base_addr_ + 0x140 + i * 4);
                    if (shorts_ & (1u << i)) counter_ = 0;
                    if (inten_ & (1u << (16 + i))) fired = true;
                }
            }
            if (inten_ & 1) fired = true; // TICK interrupt
        }

        if (fired && nvic_) {
            irq_fire_count_++;
            nvic_->set_pending(irq_);
            return Interrupt{irq_};
        }
        return {};
    }

    uint64_t next_event(uint64_t now) const override {
        if (!running_ || (!inten_ && !evten_)) return UINT64_MAX;
        uint64_t divisor = 1953ULL * (prescaler_ + 1);
        // TICK interrupt fires every RTC tick
        if (inten_ & 1) {
            uint64_t remainder = (now > last_tick_) ? (now - last_tick_) % divisor : 0;
            return now + divisor - remainder;
        }
        // Find nearest CC match with enabled interrupt or event routing
        uint64_t min_ticks = UINT64_MAX;
        for (int i = 0; i < 4; i++) {
            bool relevant = (inten_ & (1u << (16 + i))) || (evten_ & (1u << (16 + i)));
            if (!relevant) continue;
            uint32_t dist = (cc_[i] - counter_) & 0xFFFFFF;
            if (dist == 0) dist = 0x1000000;
            if (dist < min_ticks) min_ticks = dist;
        }
        if (min_ticks == UINT64_MAX) return UINT64_MAX;
        uint64_t remainder = (now > last_tick_) ? (now - last_tick_) % divisor : 0;
        return now + min_ticks * divisor - remainder;
    }
};

} // namespace pt
