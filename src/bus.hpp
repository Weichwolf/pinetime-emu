#pragma once
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace pt {

enum class Width : uint8_t { Byte = 1, Half = 2, Word = 4 };

struct Interrupt {
    uint32_t irq;
};

struct Device {
    virtual ~Device() = default;
    virtual uint32_t read(uint32_t addr, Width w) = 0;
    virtual void write(uint32_t addr, Width w, uint32_t val) = 0;
    virtual std::optional<Interrupt> tick(uint64_t /*cycles*/) { return {}; }
};

class Bus {
    struct Mapping {
        uint32_t base, size;
        Device* dev;
    };
    std::vector<Mapping> mappings_;

    uint8_t* flash_ = nullptr;
    uint32_t flash_size_ = 0;
    uint8_t* sram_ = nullptr;
    uint32_t sram_base_ = 0, sram_size_ = 0;
public:
    uint32_t watch_addr_ = 0;
    const uint32_t* cpu_pc_ = nullptr;
    const uint64_t* cpu_cy_ = nullptr;
private:

public:
    void map(uint32_t base, uint32_t size, Device* dev) {
        mappings_.push_back({base, size, dev});
    }

    void set_fast_path(uint8_t* flash, uint32_t flash_size,
                       uint8_t* sram, uint32_t sram_base, uint32_t sram_size) {
        flash_ = flash; flash_size_ = flash_size;
        sram_ = sram; sram_base_ = sram_base; sram_size_ = sram_size;
    }

    uint32_t read(uint32_t addr, Width w) const {
        // Fast path: flash
        if (addr < flash_size_) return read_mem(flash_ + addr, w);
        // Fast path: SRAM
        if (addr >= sram_base_ && addr < sram_base_ + sram_size_)
            return read_mem(sram_ + (addr - sram_base_), w);
        // Slow path: devices
        for (auto& m : mappings_)
            if (addr >= m.base && addr < m.base + m.size)
                return m.dev->read(addr - m.base, w);
        static int unmapped_r = 0;
        if (unmapped_r++ < 10) fprintf(stderr, "[BUS] Unmapped read: 0x%08X\n", addr);
        return 0;
    }

    void write(uint32_t addr, Width w, uint32_t val) {
        if (watch_addr_ && addr < watch_addr_ + 4 && addr + static_cast<uint32_t>(w) > watch_addr_) {
            fprintf(stderr, "[WATCH] 0x%08X = 0x%X (w=%d) PC=0x%08X cy=%.2fM\n",
                    addr, val, static_cast<int>(w), cpu_pc_ ? *cpu_pc_ : 0xDEAD,
                    cpu_cy_ ? *cpu_cy_ / 1e6 : -1.0);
        }
        // Fast path: SRAM
        if (addr >= sram_base_ && addr < sram_base_ + sram_size_) {
            write_mem(sram_ + (addr - sram_base_), w, val);
            return;
        }
        // Slow path: devices
        for (auto& m : mappings_)
            if (addr >= m.base && addr < m.base + m.size) {
                m.dev->write(addr - m.base, w, val);
                return;
            }
        static int unmapped_w = 0;
        if (unmapped_w++ < 10) fprintf(stderr, "[BUS] Unmapped write: 0x%08X = 0x%X\n", addr, val);
    }

    uint8_t  read8(uint32_t a)  const { return read(a, Width::Byte); }
    uint16_t read16(uint32_t a) const { return read(a, Width::Half); }
    uint32_t read32(uint32_t a) const { return read(a, Width::Word); }
    void write8(uint32_t a, uint8_t v)   { write(a, Width::Byte, v); }
    void write16(uint32_t a, uint16_t v) { write(a, Width::Half, v); }
    void write32(uint32_t a, uint32_t v) { write(a, Width::Word, v); }

private:
    static uint32_t read_mem(const uint8_t* p, Width w) {
        switch (w) {
        case Width::Byte: return *p;
        case Width::Half: return p[0] | (p[1] << 8);
        case Width::Word: return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
        }
        return 0;
    }

    static void write_mem(uint8_t* p, Width w, uint32_t v) {
        switch (w) {
        case Width::Byte: p[0] = v; break;
        case Width::Half: p[0] = v; p[1] = v >> 8; break;
        case Width::Word: p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; break;
        }
    }
};

} // namespace pt
