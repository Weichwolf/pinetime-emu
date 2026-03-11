#include "../test.hpp"
#include "device/memory.hpp"

using namespace pt;

void test_ram_read_write() {
    RAM ram(256);
    ram.write(0, Width::Word, 0xDEADBEEF);
    ASSERT_EQ(ram.read(0, Width::Word), 0xDEADBEEF);
    ram.write(4, Width::Half, 0x1234);
    ASSERT_EQ(ram.read(4, Width::Half), 0x1234);
    ram.write(6, Width::Byte, 0xAB);
    ASSERT_EQ(ram.read(6, Width::Byte), 0xAB);
}

void test_ram_little_endian() {
    RAM ram(16);
    ram.write(0, Width::Word, 0x04030201);
    ASSERT_EQ(ram.read(0, Width::Byte), 0x01);
    ASSERT_EQ(ram.read(1, Width::Byte), 0x02);
    ASSERT_EQ(ram.read(2, Width::Byte), 0x03);
    ASSERT_EQ(ram.read(3, Width::Byte), 0x04);
}

void test_ram_bounds() {
    RAM ram(16);
    ram.write(20, Width::Word, 0x12345678); // out of bounds
    ASSERT_EQ(ram.read(20, Width::Word), 0u);
}

void test_ram_load() {
    RAM ram(256);
    uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    ram.load(data, sizeof(data), 0x10);
    ASSERT_EQ(ram.read(0x10, Width::Word), 0xDDCCBBAAu);
}

void test_flash_init_ff() {
    Flash flash(256);
    ASSERT_EQ(flash.read(0, Width::Word), 0xFFFFFFFF);
}

void test_flash_write_protected() {
    Flash flash(256);
    flash.write(0, Width::Word, 0x12345678); // no NVMC config -> dropped
    ASSERT_EQ(flash.read(0, Width::Word), 0xFFFFFFFF);
}

void test_flash_write_enabled() {
    Flash flash(256);
    uint32_t config = 1; // write enabled
    flash.set_nvmc_config(&config);
    flash.write(0, Width::Word, 0x12345678);
    // Flash AND semantics: 0xFF & 0x12 = 0x12, etc.
    ASSERT_EQ(flash.read(0, Width::Word), 0x12345678u);
}

void test_flash_and_semantics() {
    Flash flash(256);
    uint32_t config = 1;
    flash.set_nvmc_config(&config);
    flash.write(0, Width::Word, 0xF0F0F0F0);
    flash.write(0, Width::Word, 0x0F0F0F0F);
    ASSERT_EQ(flash.read(0, Width::Word), 0x00000000u); // F0 & 0F = 00
}

void test_flash_size() {
    Flash flash(512 * 1024);
    ASSERT_EQ(flash.size(), 512u * 1024);
}

void test_ram_zero_init() {
    RAM ram(64);
    for (uint32_t i = 0; i < 64; i += 4)
        ASSERT_EQ(ram.read(i, Width::Word), 0u);
}

void test_ram_half_word_alignment() {
    RAM ram(16);
    ram.write(2, Width::Half, 0xABCD);
    ASSERT_EQ(ram.read(2, Width::Half), 0xABCD);
    // Byte access within same half-word
    ASSERT_EQ(ram.read(2, Width::Byte), 0xCD);
    ASSERT_EQ(ram.read(3, Width::Byte), 0xAB);
}

void test_flash_byte_write() {
    Flash flash(256);
    uint32_t config = 1;
    flash.set_nvmc_config(&config);
    flash.write(0, Width::Byte, 0x42);
    ASSERT_EQ(flash.read(0, Width::Byte), 0x42);
    // Adjacent bytes unchanged
    ASSERT_EQ(flash.read(1, Width::Byte), 0xFF);
}

void test_flash_half_write() {
    Flash flash(256);
    uint32_t config = 1;
    flash.set_nvmc_config(&config);
    flash.write(0, Width::Half, 0x1234);
    ASSERT_EQ(flash.read(0, Width::Half), 0x1234);
}

void test_ram_large_load() {
    RAM ram(1024);
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = i;
    ram.load(data, sizeof(data), 0);
    ASSERT_EQ(ram.read(0, Width::Byte), 0x00);
    ASSERT_EQ(ram.read(255, Width::Byte), 0xFF);
    ASSERT_EQ(ram.read(0, Width::Word), 0x03020100u);
}

int main() {
    RUN(test_ram_read_write);
    RUN(test_ram_little_endian);
    RUN(test_ram_bounds);
    RUN(test_ram_load);
    RUN(test_flash_init_ff);
    RUN(test_flash_write_protected);
    RUN(test_flash_write_enabled);
    RUN(test_flash_and_semantics);
    RUN(test_flash_size);
    RUN(test_ram_zero_init);
    RUN(test_ram_half_word_alignment);
    RUN(test_flash_byte_write);
    RUN(test_flash_half_write);
    RUN(test_ram_large_load);
    TEST_REPORT();
}
