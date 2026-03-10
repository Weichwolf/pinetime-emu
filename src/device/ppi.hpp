#pragma once
#include "../bus.hpp"
#include <array>

namespace pt {

// nRF52 PPI (Programmable Peripheral Interconnect) at 0x4001F000
// Connects peripheral events to tasks via hardware routing.
// On tick(), polls enabled channels: if EEP event register rose to 1,
// writes 1 to TEP task register (and fork TEP if set).
class PPI : public Device {
    struct Channel {
        uint32_t eep = 0;  // Event End Point (absolute address)
        uint32_t tep = 0;  // Task End Point (absolute address)
    };
    std::array<Channel, 20> ch_{};          // 20 programmable channels
    std::array<uint32_t, 20> fork_tep_{};   // Fork task endpoints
    uint32_t chen_ = 0;                     // Channel enable
    std::array<uint32_t, 6> chg_{};         // Channel groups
    std::array<uint32_t, 20> prev_ev_{};    // Previous event state (edge detection)

    Bus* bus_ = nullptr;

public:
    void set_bus(Bus* bus) { bus_ = bus; }

    uint32_t read(uint32_t addr, Width) override {
        if (addr == 0x500) return chen_;
        if (addr >= 0x800 && addr < 0x818) return chg_[(addr - 0x800) / 4];
        if (addr >= 0x510 && addr < 0x5B0 && ((addr - 0x510) % 8 == 0))
            return ch_[(addr - 0x510) / 8].eep;
        if (addr >= 0x514 && addr < 0x5B4 && ((addr - 0x510) % 8 == 4))
            return ch_[(addr - 0x510) / 8].tep;
        if (addr >= 0x910 && addr < 0x960)
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
        if (addr >= 0x910 && addr < 0x960) {
            fork_tep_[(addr - 0x910) / 4] = val; return;
        }
    }

    std::optional<Interrupt> tick(uint64_t) override {
        if (!bus_ || !chen_) return {};

        for (int i = 0; i < 20; i++) {
            if (!(chen_ & (1u << i))) continue;
            uint32_t eep = ch_[i].eep;
            uint32_t tep = ch_[i].tep;
            if (!eep || !tep) continue;

            uint32_t ev = bus_->read32(eep);
            if (ev && !prev_ev_[i]) {
                // Rising edge: trigger task
                bus_->write32(tep, 1);
                if (fork_tep_[i])
                    bus_->write32(fork_tep_[i], 1);
            }
            prev_ev_[i] = ev;
        }
        return {};
    }
};

} // namespace pt
