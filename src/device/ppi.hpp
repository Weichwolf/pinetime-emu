#pragma once
#include "../bus.hpp"
#include <array>

namespace pt {

// nRF52 PPI (Programmable Peripheral Interconnect) at 0x4001F000
// Connects peripheral events to tasks via hardware routing.
// On tick(), polls enabled channels: if EEP event register rose to 1,
// writes 1 to TEP task register (and fork TEP if set).
// Channels 0-19: programmable (EEP/TEP set via registers)
// Channels 20-31: fixed (hardcoded EEP/TEP per nRF52832 spec)
class PPI : public Device {
    struct Channel {
        uint32_t eep = 0;  // Event End Point (absolute address)
        uint32_t tep = 0;  // Task End Point (absolute address)
    };
    std::array<Channel, 20> ch_{};          // 20 programmable channels
    std::array<uint32_t, 32> fork_tep_{};   // Fork task endpoints (all 32 channels)
    uint32_t chen_ = 0;                     // Channel enable (bits 0-31)
    std::array<uint32_t, 6> chg_{};         // Channel groups
    std::array<uint32_t, 32> prev_ev_{};    // Previous event state (edge detection)

    Bus* bus_ = nullptr;

    // nRF52832 fixed PPI channels (20-31)
    // Peripheral base addresses:
    //   TIMER0=0x40008000  RADIO=0x40001000  CLOCK=0x40000000
    //   RTC0=0x4000B000    AAR=0x4000F000    CCM=0x4000F000
    static constexpr Channel fixed_[12] = {
        {0x40008140, 0x40001000}, // CH20: TIMER0.COMPARE[0] → RADIO.TXEN
        {0x40008140, 0x40001004}, // CH21: TIMER0.COMPARE[0] → RADIO.RXEN
        {0x40008144, 0x40001010}, // CH22: TIMER0.COMPARE[1] → RADIO.DISABLE
        {0x40001128, 0x4000F000}, // CH23: RADIO.BCMATCH → AAR.START
        {0x40001100, 0x4000F000}, // CH24: RADIO.READY → CCM.KSGEN
        {0x40001104, 0x4000F004}, // CH25: RADIO.ADDRESS → CCM.CRYPT
        {0x40001104, 0x40008044}, // CH26: RADIO.ADDRESS → TIMER0.CAPTURE[1]
        {0x4000110C, 0x40008048}, // CH27: RADIO.END → TIMER0.CAPTURE[2]
        {0x4000B140, 0x40001000}, // CH28: RTC0.COMPARE[0] → RADIO.TXEN
        {0x4000B140, 0x40001004}, // CH29: RTC0.COMPARE[0] → RADIO.RXEN
        {0x4000B140, 0x4000100C}, // CH30: RTC0.COMPARE[0] → RADIO.STOP
        {0x4000B140, 0x40008000}, // CH31: RTC0.COMPARE[0] → TIMER0.START
    };

public:
    void set_bus(Bus* bus) { bus_ = bus; }

    // Instant event notification — called by peripherals when setting an event register.
    // Checks all enabled channels and fires matching TEPs immediately.
    void on_event(uint32_t event_addr) {
        if (!bus_ || !chen_) return;
        for (int i = 0; i < 20; i++) {
            if (!(chen_ & (1u << i))) continue;
            if (ch_[i].eep == event_addr) {
                bus_->write32(ch_[i].tep, 1);
                if (fork_tep_[i]) bus_->write32(fork_tep_[i], 1);
            }
        }
        uint32_t fixed_mask = chen_ >> 20;
        for (int i = 0; i < 12 && fixed_mask; i++, fixed_mask >>= 1) {
            if (!(fixed_mask & 1)) continue;
            if (fixed_[i].eep == event_addr) {
                bus_->write32(fixed_[i].tep, 1);
                if (fork_tep_[20 + i]) bus_->write32(fork_tep_[20 + i], 1);
            }
        }
    }

    uint32_t read(uint32_t addr, Width) override {
        if (addr == 0x500) return chen_;
        if (addr >= 0x800 && addr < 0x818) return chg_[(addr - 0x800) / 4];
        if (addr >= 0x510 && addr < 0x5B0 && ((addr - 0x510) % 8 == 0))
            return ch_[(addr - 0x510) / 8].eep;
        if (addr >= 0x514 && addr < 0x5B4 && ((addr - 0x510) % 8 == 4))
            return ch_[(addr - 0x510) / 8].tep;
        if (addr >= 0x910 && addr < 0x990)
            return fork_tep_[(addr - 0x910) / 4];
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        // TASKS_CHG[n].EN at 0x000 + n*8
        if (addr < 0x030 && (addr % 8 == 0)) {
            if (val) chen_ |= chg_[addr / 8];
            return;
        }
        // TASKS_CHG[n].DIS at 0x004 + n*8
        if (addr < 0x034 && (addr % 8 == 4)) {
            if (val) chen_ &= ~chg_[(addr - 4) / 8];
            return;
        }
        if (addr == 0x500) { chen_ = val; return; }
        if (addr == 0x504) { chen_ |= val; return; }
        if (addr == 0x508) { chen_ &= ~val; return; }
        if (addr >= 0x800 && addr < 0x818) { chg_[(addr - 0x800) / 4] = val; return; }
        if (addr >= 0x510 && addr < 0x5B0 && ((addr - 0x510) % 8 == 0)) {
            ch_[(addr - 0x510) / 8].eep = val; return;
        }
        if (addr >= 0x514 && addr < 0x5B4 && ((addr - 0x510) % 8 == 4)) {
            ch_[(addr - 0x510) / 8].tep = val; return;
        }
        if (addr >= 0x910 && addr < 0x990) {
            fork_tep_[(addr - 0x910) / 4] = val; return;
        }
    }

    std::optional<Interrupt> tick(uint64_t) override {
        if (!bus_ || !chen_) return {};

        // Programmable channels 0-19
        for (int i = 0; i < 20; i++) {
            if (!(chen_ & (1u << i))) continue;
            uint32_t eep = ch_[i].eep;
            uint32_t tep = ch_[i].tep;
            if (!eep || !tep) continue;
            poll_channel(i, eep, tep);
        }

        // Fixed channels 20-31
        uint32_t fixed_mask = chen_ >> 20;
        for (int i = 0; i < 12 && fixed_mask; i++, fixed_mask >>= 1) {
            if (!(fixed_mask & 1)) continue;
            poll_channel(20 + i, fixed_[i].eep, fixed_[i].tep);
        }

        return {};
    }

private:
    void poll_channel(int idx, uint32_t eep, uint32_t tep) {
        uint32_t ev = bus_->read32(eep);
        if (ev && !prev_ev_[idx]) {
            bus_->write32(tep, 1);
            if (fork_tep_[idx])
                bus_->write32(fork_tep_[idx], 1);
        }
        prev_ev_[idx] = ev;
    }
};

} // namespace pt
