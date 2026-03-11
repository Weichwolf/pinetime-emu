#pragma once
#include "../bus.hpp"
#include "../cpu.hpp"
#include "memory.hpp"
#include <cstdio>

namespace pt {

// nRF52 NVMC (Non-Volatile Memory Controller) at 0x4001E000
// Controls flash write/erase operations
class NVMC : public Device {
    uint32_t config_ = 0;     // 0=ReadOnly, 1=Write, 2=Erase
    uint32_t icachecnf_ = 0;
    Flash* flash_ = nullptr;
    CPU* cpu_ = nullptr;

public:
    void set_flash(Flash* f) { flash_ = f; }
    void set_cpu(CPU* c) { cpu_ = c; }
    const uint32_t* config_ptr() const { return &config_; }

    // Is flash writable?
    bool write_enabled() const { return config_ == 1; }
    bool erase_enabled() const { return config_ == 2; }

    uint32_t read(uint32_t addr, Width /*w*/) override {
        switch (addr) {
        case 0x400: return 1; // READY (always ready)
        case 0x408: return 1; // READYNEXT
        case 0x504: return config_;
        case 0x540: return icachecnf_;
        case 0x548: return 0; // IHIT
        case 0x54C: return 0; // IMISS
        }
        return 0;
    }

    void write(uint32_t addr, Width /*w*/, uint32_t val) override {
        switch (addr) {
        case 0x504:
            fprintf(stderr, "[NVMC] CONFIG %u → %u\n", config_, val & 3);
            config_ = val & 3; break;
        case 0x508: // ERASEPAGE
            if (erase_enabled() && flash_) {
                uint32_t page_addr = val & ~0xFFFu;
                uint8_t* data = flash_->data();
                if (page_addr + 0x1000 <= flash_->size()) {
                    memset(data + page_addr, 0xFF, 0x1000);
                    if (cpu_) cpu_->invalidate_cache(page_addr, 0x1000);
                }
            }
            break;
        case 0x50C: // ERASEALL
            if (erase_enabled() && flash_ && val) {
                memset(flash_->data(), 0xFF, flash_->size());
                if (cpu_) cpu_->invalidate_cache(0, flash_->size());
            }
            break;
        case 0x510: break; // ERASEUICR
        case 0x540: icachecnf_ = val; break;
        }
    }
};

} // namespace pt
