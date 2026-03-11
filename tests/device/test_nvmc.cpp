#include "../test.hpp"
#include "device/nvmc.hpp"
#include "device/memory.hpp"

using namespace pt;

void test_always_ready() {
    NVMC nvmc;
    ASSERT_EQ(nvmc.read(0x400, Width::Word), 1u); // READY
    ASSERT_EQ(nvmc.read(0x408, Width::Word), 1u); // READYNEXT
}

void test_config() {
    NVMC nvmc;
    ASSERT_EQ(nvmc.read(0x504, Width::Word), 0u); // Default: ReadOnly
    nvmc.write(0x504, Width::Word, 1);
    ASSERT_EQ(nvmc.read(0x504, Width::Word), 1u);
    ASSERT(nvmc.write_enabled());
    nvmc.write(0x504, Width::Word, 2);
    ASSERT(nvmc.erase_enabled());
    nvmc.write(0x504, Width::Word, 0xFF); // Mask to 2 bits
    ASSERT_EQ(nvmc.read(0x504, Width::Word), 3u);
}

void test_erase_page() {
    NVMC nvmc;
    Flash flash(8192);

    // Write some data via NVMC write-enable
    uint32_t cfg = 1;
    flash.set_nvmc_config(&cfg);
    flash.write(0, Width::Word, 0x12345678);
    ASSERT_EQ(flash.read(0, Width::Word), 0x12345678u);

    nvmc.set_flash(&flash);
    nvmc.write(0x504, Width::Word, 2); // Erase mode
    nvmc.write(0x508, Width::Word, 0); // ERASEPAGE at 0
    ASSERT_EQ(flash.read(0, Width::Word), 0xFFFFFFFF); // Erased
}

void test_erase_all() {
    NVMC nvmc;
    Flash flash(8192);
    uint32_t cfg = 1;
    flash.set_nvmc_config(&cfg);
    flash.write(0, Width::Word, 0);
    flash.write(4096, Width::Word, 0);

    nvmc.set_flash(&flash);
    nvmc.write(0x504, Width::Word, 2);
    nvmc.write(0x50C, Width::Word, 1); // ERASEALL
    ASSERT_EQ(flash.read(0, Width::Word), 0xFFFFFFFF);
    ASSERT_EQ(flash.read(4096, Width::Word), 0xFFFFFFFF);
}

void test_icachecnf() {
    NVMC nvmc;
    nvmc.write(0x540, Width::Word, 1);
    ASSERT_EQ(nvmc.read(0x540, Width::Word), 1u);
}

void test_config_ptr() {
    NVMC nvmc;
    nvmc.write(0x504, Width::Word, 1);
    ASSERT_EQ(*nvmc.config_ptr(), 1u);
}

void test_erase_without_config() {
    NVMC nvmc;
    Flash flash(4096);
    uint32_t cfg = 1;
    flash.set_nvmc_config(&cfg);
    flash.write(0, Width::Word, 0);

    nvmc.set_flash(&flash);
    nvmc.write(0x504, Width::Word, 0); // ReadOnly mode
    nvmc.write(0x508, Width::Word, 0); // Try ERASEPAGE
    // Should NOT erase (not in erase mode)
    ASSERT_EQ(flash.read(0, Width::Word), 0u);
}

void test_config_transitions() {
    NVMC nvmc;
    // ReadOnly -> Write -> Erase -> ReadOnly
    nvmc.write(0x504, Width::Word, 0);
    ASSERT(!nvmc.write_enabled());
    ASSERT(!nvmc.erase_enabled());

    nvmc.write(0x504, Width::Word, 1);
    ASSERT(nvmc.write_enabled());

    nvmc.write(0x504, Width::Word, 2);
    ASSERT(nvmc.erase_enabled());

    nvmc.write(0x504, Width::Word, 0);
    ASSERT(!nvmc.write_enabled());
    ASSERT(!nvmc.erase_enabled());
}

int main() {
    RUN(test_always_ready);
    RUN(test_config);
    RUN(test_erase_page);
    RUN(test_erase_all);
    RUN(test_icachecnf);
    RUN(test_config_ptr);
    RUN(test_erase_without_config);
    RUN(test_config_transitions);
    TEST_REPORT();
}
