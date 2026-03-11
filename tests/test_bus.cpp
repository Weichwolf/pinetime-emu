#include "test.hpp"
#include "bus.hpp"

using namespace pt;

struct DummyDevice : Device {
    uint32_t last_read_addr = 0xDEAD;
    uint32_t last_write_addr = 0xDEAD;
    uint32_t last_write_val = 0;
    uint32_t read_val = 0x42;

    uint32_t read(uint32_t addr, Width) override { last_read_addr = addr; return read_val; }
    void write(uint32_t addr, Width, uint32_t val) override { last_write_addr = addr; last_write_val = val; }
};

void test_fast_path_sram() {
    Bus bus;
    uint8_t sram[256]{};
    bus.set_fast_path(nullptr, 0, sram, 0x20000000, sizeof(sram));
    bus.write32(0x20000000, 0xDEADBEEF);
    ASSERT_EQ(bus.read32(0x20000000), 0xDEADBEEF);
    bus.write8(0x20000010, 0xAB);
    ASSERT_EQ(bus.read8(0x20000010), 0xAB);
    bus.write16(0x20000020, 0x1234);
    ASSERT_EQ(bus.read16(0x20000020), 0x1234);
}

void test_fast_path_flash() {
    Bus bus;
    uint8_t flash[64]{};
    flash[0] = 0x11; flash[1] = 0x22; flash[2] = 0x33; flash[3] = 0x44;
    bus.set_fast_path(flash, sizeof(flash), nullptr, 0x20000000, 0);
    ASSERT_EQ(bus.read32(0), 0x44332211);
    ASSERT_EQ(bus.read8(0), 0x11);
    ASSERT_EQ(bus.read16(0), 0x2211);
}

void test_device_mapping() {
    Bus bus;
    DummyDevice dev;
    bus.map(0x40001000, 0x1000, &dev);
    dev.read_val = 0xBEEF;
    ASSERT_EQ(bus.read32(0x40001100), 0xBEEF);
    ASSERT_EQ(dev.last_read_addr, 0x100u);
    bus.write32(0x40001200, 0xCAFE);
    ASSERT_EQ(dev.last_write_addr, 0x200u);
    ASSERT_EQ(dev.last_write_val, 0xCAFEu);
}

void test_unmapped_returns_zero() {
    Bus bus;
    ASSERT_EQ(bus.read32(0xFFFF0000), 0u);
}

void test_sram_little_endian() {
    Bus bus;
    uint8_t sram[16]{};
    bus.set_fast_path(nullptr, 0, sram, 0x20000000, sizeof(sram));
    bus.write32(0x20000000, 0x04030201);
    ASSERT_EQ(sram[0], 0x01);
    ASSERT_EQ(sram[1], 0x02);
    ASSERT_EQ(sram[2], 0x03);
    ASSERT_EQ(sram[3], 0x04);
}

int main() {
    RUN(test_fast_path_sram);
    RUN(test_fast_path_flash);
    RUN(test_device_mapping);
    RUN(test_unmapped_returns_zero);
    RUN(test_sram_little_endian);
    TEST_REPORT();
}
