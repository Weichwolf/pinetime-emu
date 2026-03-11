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

void test_multi_device_mapping() {
    Bus bus;
    DummyDevice dev1, dev2;
    dev1.read_val = 0xAA;
    dev2.read_val = 0xBB;
    bus.map(0x40001000, 0x1000, &dev1);
    bus.map(0x40002000, 0x1000, &dev2);
    ASSERT_EQ(bus.read32(0x40001000), 0xAA);
    ASSERT_EQ(bus.read32(0x40002000), 0xBB);
}

void test_width_byte_half_word() {
    Bus bus;
    uint8_t sram[16]{};
    bus.set_fast_path(nullptr, 0, sram, 0x20000000, sizeof(sram));
    bus.write32(0x20000000, 0xDDCCBBAA);
    ASSERT_EQ(bus.read8(0x20000000), 0xAA);
    ASSERT_EQ(bus.read8(0x20000001), 0xBB);
    ASSERT_EQ(bus.read16(0x20000000), 0xBBAA);
    ASSERT_EQ(bus.read16(0x20000002), 0xDDCC);
    ASSERT_EQ(bus.read32(0x20000000), 0xDDCCBBAA);
}

void test_sram_write_byte_preserves() {
    Bus bus;
    uint8_t sram[16]{};
    bus.set_fast_path(nullptr, 0, sram, 0x20000000, sizeof(sram));
    bus.write32(0x20000000, 0x12345678);
    bus.write8(0x20000001, 0xFF);
    ASSERT_EQ(bus.read32(0x20000000), 0x1234FF78u);
}

void test_sram_write_half_preserves() {
    Bus bus;
    uint8_t sram[16]{};
    bus.set_fast_path(nullptr, 0, sram, 0x20000000, sizeof(sram));
    bus.write32(0x20000000, 0x12345678);
    bus.write16(0x20000002, 0xABCD);
    ASSERT_EQ(bus.read32(0x20000000), 0xABCD5678u);
}

void test_device_offset_calculation() {
    Bus bus;
    DummyDevice dev;
    bus.map(0x40008000, 0x1000, &dev);
    bus.read32(0x40008540);
    ASSERT_EQ(dev.last_read_addr, 0x540u);
    bus.write32(0x40008200, 0x42);
    ASSERT_EQ(dev.last_write_addr, 0x200u);
}

int main() {
    RUN(test_fast_path_sram);
    RUN(test_fast_path_flash);
    RUN(test_device_mapping);
    RUN(test_unmapped_returns_zero);
    RUN(test_sram_little_endian);
    RUN(test_multi_device_mapping);
    RUN(test_width_byte_half_word);
    RUN(test_sram_write_byte_preserves);
    RUN(test_sram_write_half_preserves);
    RUN(test_device_offset_calculation);
    TEST_REPORT();
}
