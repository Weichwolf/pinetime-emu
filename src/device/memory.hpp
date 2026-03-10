#pragma once
#include "../bus.hpp"
#include <cstring>
#include <vector>

namespace pt {

class RAM : public Device {
    std::vector<uint8_t> mem_;
public:
    explicit RAM(uint32_t size) : mem_(size, 0) {}

    uint32_t read(uint32_t addr, Width w) override {
        if (addr + static_cast<uint32_t>(w) > mem_.size()) return 0;
        return read_le(&mem_[addr], w);
    }

    void write(uint32_t addr, Width w, uint32_t val) override {
        if (addr + static_cast<uint32_t>(w) > mem_.size()) return;
        write_le(&mem_[addr], w, val);
    }

    uint8_t* data() { return mem_.data(); }
    uint32_t size() const { return mem_.size(); }
    void load(const uint8_t* src, uint32_t len, uint32_t offset = 0) {
        if (offset + len <= mem_.size()) memcpy(&mem_[offset], src, len);
    }

private:
    static uint32_t read_le(const uint8_t* p, Width w) {
        switch (w) {
        case Width::Byte: return *p;
        case Width::Half: return p[0] | (p[1] << 8);
        case Width::Word: return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
        }
        return 0;
    }

    static void write_le(uint8_t* p, Width w, uint32_t v) {
        switch (w) {
        case Width::Byte: p[0] = v; break;
        case Width::Half: p[0] = v; p[1] = v >> 8; break;
        case Width::Word: p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; break;
        }
    }
};

class Flash : public Device {
    std::vector<uint8_t> mem_;
public:
    explicit Flash(uint32_t size) : mem_(size, 0xFF) {}

    uint32_t read(uint32_t addr, Width w) override {
        if (addr + static_cast<uint32_t>(w) > mem_.size()) return 0;
        switch (w) {
        case Width::Byte: return mem_[addr];
        case Width::Half: return mem_[addr] | (mem_[addr+1] << 8);
        case Width::Word: return mem_[addr] | (mem_[addr+1] << 8) |
                                 (mem_[addr+2] << 16) | (mem_[addr+3] << 24);
        }
        return 0;
    }

    void write(uint32_t /*addr*/, Width /*w*/, uint32_t /*val*/) override {
        // Flash is read-only during normal operation
    }

    uint8_t* data() { return mem_.data(); }
    uint32_t size() const { return mem_.size(); }

    bool load_file(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len > 0 && static_cast<uint32_t>(len) <= mem_.size())
            fread(mem_.data(), 1, len, f);
        fclose(f);
        return true;
    }
};

} // namespace pt
