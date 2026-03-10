#pragma once
#include "../bus.hpp"
#include <array>

namespace pt {

// nRF52 PPI (Programmable Peripheral Interconnect) at 0x4001F000
// Connects peripheral events to tasks automatically
// For emulation: store config, no actual event routing needed
class PPI : public Device {
    struct Channel {
        uint32_t eep = 0;  // Event End Point
        uint32_t tep = 0;  // Task End Point
    };
    std::array<Channel, 20> ch_{};          // 20 programmable channels
    std::array<uint32_t, 20> fork_tep_{};   // Fork task endpoints
    uint32_t chen_ = 0;                     // Channel enable
    std::array<uint32_t, 6> chg_{};         // Channel groups

public:
    uint32_t read(uint32_t addr, Width) override {
        if (addr == 0x500) return chen_;
        // CHG[n] at 0x800 + n*4
        if (addr >= 0x800 && addr < 0x818) return chg_[(addr - 0x800) / 4];
        // CH[n].EEP at 0x510 + n*8
        if (addr >= 0x510 && addr < 0x5B0 && ((addr - 0x510) % 8 == 0))
            return ch_[(addr - 0x510) / 8].eep;
        // CH[n].TEP at 0x514 + n*8
        if (addr >= 0x514 && addr < 0x5B4 && ((addr - 0x510) % 8 == 4))
            return ch_[(addr - 0x510) / 8].tep;
        // FORK[n].TEP at 0x910 + n*4
        if (addr >= 0x910 && addr < 0x960)
            return fork_tep_[(addr - 0x910) / 4];
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        // TASKS_CHG[n].EN at 0x000 + n*8
        if (addr < 0x030 && (addr % 8 == 0)) {
            if (val) chen_ |= (1u << (addr / 8));
            return;
        }
        // TASKS_CHG[n].DIS at 0x004 + n*8
        if (addr < 0x034 && (addr % 8 == 4)) {
            if (val) chen_ &= ~(1u << (addr / 8));
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
};

} // namespace pt
