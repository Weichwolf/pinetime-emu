#include "../test.hpp"
#include "device/spi_flash.hpp"

using namespace pt;

void test_initial_ff() {
    SpiFlash flash(4096);
    ASSERT_EQ(flash.read(0, Width::Word), 0xFFFFFFFF);
}

void test_jedec_id() {
    SpiFlash flash(4096);
    uint8_t tx[] = {0x9F, 0, 0, 0};
    uint8_t rx[4]{};
    flash.transfer(tx, 4, rx, 4);
    ASSERT_EQ(rx[1], 0x0B);
    ASSERT_EQ(rx[2], 0x40);
    ASSERT_EQ(rx[3], 0x16);
}

void test_read_status() {
    SpiFlash flash(4096);
    flash.reset_cmd();
    uint8_t tx = 0x05;
    flash.transfer(&tx, 1, nullptr, 0);
    // Now read status byte
    uint8_t rx = 0xFF;
    flash.transfer(nullptr, 0, &rx, 1);
    ASSERT_EQ(rx & 0x03, 0u); // WEL=0, WIP=0
}

void test_wren() {
    SpiFlash flash(4096);
    uint8_t tx = 0x06; // WREN
    flash.transfer(&tx, 1, nullptr, 0);
    // Check status
    flash.reset_cmd();
    tx = 0x05;
    flash.transfer(&tx, 1, nullptr, 0);
    uint8_t rx = 0;
    flash.transfer(nullptr, 0, &rx, 1);
    ASSERT(rx & 0x02); // WEL=1
}

void test_page_program() {
    SpiFlash flash(4096);
    // WREN
    uint8_t wren_tx[] = {0x06};
    flash.transfer(wren_tx, 1, nullptr, 0);

    // PAGE PROGRAM: cmd + 3 addr bytes + data
    uint8_t tx[] = {0x02, 0x00, 0x00, 0x10, 0xAA, 0xBB, 0xCC};
    flash.transfer(tx, 7, nullptr, 0);

    // Read back via Device interface
    ASSERT_EQ(flash.read(0x10, Width::Byte), 0xAA);
    ASSERT_EQ(flash.read(0x11, Width::Byte), 0xBB);
    ASSERT_EQ(flash.read(0x12, Width::Byte), 0xCC);
}

void test_flash_and_semantics() {
    SpiFlash flash(4096);
    // Write 0xF0
    uint8_t tx1[] = {0x06}; flash.transfer(tx1, 1, nullptr, 0);
    uint8_t tx2[] = {0x02, 0, 0, 0, 0xF0}; flash.transfer(tx2, 5, nullptr, 0);
    ASSERT_EQ(flash.read(0, Width::Byte), 0xF0);

    // Write 0x0F -> AND with 0xF0 = 0x00
    uint8_t tx3[] = {0x06}; flash.transfer(tx3, 1, nullptr, 0);
    uint8_t tx4[] = {0x02, 0, 0, 0, 0x0F}; flash.transfer(tx4, 5, nullptr, 0);
    ASSERT_EQ(flash.read(0, Width::Byte), 0x00);
}

void test_sector_erase() {
    SpiFlash flash(8192);
    // Write something
    flash.data()[0] = 0xAA;
    flash.data()[4095] = 0xBB;

    uint8_t wren[] = {0x06}; flash.transfer(wren, 1, nullptr, 0);
    uint8_t tx[] = {0x20, 0x00, 0x00, 0x00}; // 4K erase at 0
    flash.transfer(tx, 4, nullptr, 0);

    ASSERT_EQ(flash.data()[0], 0xFF);
    ASSERT_EQ(flash.data()[4095], 0xFF);
    ASSERT_EQ(flash.data()[4096], 0xFF); // Next sector untouched (was 0xFF)
}

void test_chip_erase() {
    SpiFlash flash(4096);
    flash.data()[0] = 0x00;
    flash.data()[4095] = 0x00;

    uint8_t wren[] = {0x06}; flash.transfer(wren, 1, nullptr, 0);
    uint8_t tx[] = {0xC7}; // Chip erase
    flash.transfer(tx, 1, nullptr, 0);

    ASSERT_EQ(flash.data()[0], 0xFF);
    ASSERT_EQ(flash.data()[4095], 0xFF);
}

void test_read_cmd() {
    SpiFlash flash(4096);
    flash.data()[0x100] = 0x42;
    flash.data()[0x101] = 0x43;

    uint8_t tx[] = {0x03, 0x00, 0x01, 0x00}; // READ at 0x100
    uint8_t rx[6]{};
    flash.transfer(tx, 4, rx, 6);
    ASSERT_EQ(rx[4], 0x42);
    ASSERT_EQ(rx[5], 0x43);
}

void test_fast_read() {
    SpiFlash flash(4096);
    flash.data()[0x200] = 0xEE;

    uint8_t tx[] = {0x0B, 0x00, 0x02, 0x00, 0x00}; // FAST READ + dummy
    uint8_t rx[6]{};
    flash.transfer(tx, 5, rx, 6);
    ASSERT_EQ(rx[5], 0xEE);
}

void test_device_interface() {
    SpiFlash flash(4096);
    flash.write(0, Width::Word, 0x12345678);
    ASSERT_EQ(flash.read(0, Width::Word), 0x12345678u);
    flash.write(4, Width::Half, 0xABCD);
    ASSERT_EQ(flash.read(4, Width::Half), 0xABCDu);
    flash.write(6, Width::Byte, 0xFF);
    ASSERT_EQ(flash.read(6, Width::Byte), 0xFFu);
}

void test_write_without_wren() {
    SpiFlash flash(4096);
    // Write without WREN -> should not change data
    uint8_t tx[] = {0x02, 0, 0, 0, 0xAA};
    flash.transfer(tx, 5, nullptr, 0);
    ASSERT_EQ(flash.data()[0], 0xFF); // Unchanged
}

void test_wrdi() {
    SpiFlash flash(4096);
    // WREN then WRDI
    uint8_t wren[] = {0x06}; flash.transfer(wren, 1, nullptr, 0);
    flash.reset_cmd();
    uint8_t wrdi[] = {0x04}; flash.transfer(wrdi, 1, nullptr, 0);
    flash.reset_cmd();
    // Check status: WEL should be 0
    uint8_t tx = 0x05; flash.transfer(&tx, 1, nullptr, 0);
    uint8_t rx = 0xFF; flash.transfer(nullptr, 0, &rx, 1);
    ASSERT_EQ(rx & 0x02, 0u);
}

void test_release_deep_powerdown() {
    SpiFlash flash(4096);
    uint8_t tx[] = {0xAB};
    flash.transfer(tx, 1, nullptr, 0);
    // Should not crash, just process command
}

void test_reset_cmd_clears_state() {
    SpiFlash flash(4096);
    // Start a READ command
    uint8_t tx[] = {0x03, 0x00};
    flash.transfer(tx, 2, nullptr, 0);
    // CS deselect (reset_cmd)
    flash.reset_cmd();
    // Now JEDEC ID should work fresh
    uint8_t id_tx[] = {0x9F, 0, 0, 0};
    uint8_t rx[4]{};
    flash.transfer(id_tx, 4, rx, 4);
    ASSERT_EQ(rx[1], 0x0B);
}

void test_sequential_page_program() {
    SpiFlash flash(4096);
    // Write page 1
    uint8_t w1[] = {0x06}; flash.transfer(w1, 1, nullptr, 0);
    uint8_t p1[] = {0x02, 0, 0, 0, 0x11, 0x22};
    flash.transfer(p1, 6, nullptr, 0);

    // Write page 2 (new CS cycle)
    flash.reset_cmd();
    uint8_t w2[] = {0x06}; flash.transfer(w2, 1, nullptr, 0);
    flash.reset_cmd();
    uint8_t p2[] = {0x02, 0, 0, 0x10, 0x33, 0x44};
    flash.transfer(p2, 6, nullptr, 0);

    ASSERT_EQ(flash.read(0x00, Width::Byte), 0x11);
    ASSERT_EQ(flash.read(0x01, Width::Byte), 0x22);
    ASSERT_EQ(flash.read(0x10, Width::Byte), 0x33);
    ASSERT_EQ(flash.read(0x11, Width::Byte), 0x44);
}

void test_block_erase_64k() {
    SpiFlash flash(131072); // 128KB
    // Write at start of 64KB block 1 (offset 0x10000)
    flash.data()[0x10000] = 0x42;
    flash.data()[0x1FFFF] = 0x43;
    // And outside block 1
    flash.data()[0x0FFFF] = 0xAA; // End of block 0

    uint8_t wren[] = {0x06}; flash.transfer(wren, 1, nullptr, 0);
    // 0xD8, addr = 0x01, 0x00, 0x00 → addr = 0x010000
    uint8_t tx[] = {0xD8, 0x01, 0x00, 0x00};
    flash.transfer(tx, 4, nullptr, 0);

    ASSERT_EQ(flash.data()[0x10000], 0xFF); // Erased
    ASSERT_EQ(flash.data()[0x1FFFF], 0xFF); // Erased
    ASSERT_EQ(flash.data()[0x0FFFF], 0xAA); // Untouched
}

void test_split_tx_rx_read() {
    SpiFlash flash(4096);
    flash.data()[0x100] = 0xEE;
    flash.data()[0x101] = 0xFF;

    // First transfer: TX only (cmd + addr)
    uint8_t tx[] = {0x03, 0x00, 0x01, 0x00}; // READ at 0x100
    flash.transfer(tx, 4, nullptr, 0);

    // Second transfer: RX only (data phase)
    uint8_t rx[2]{};
    flash.transfer(nullptr, 0, rx, 2);
    ASSERT_EQ(rx[0], 0xEE);
    ASSERT_EQ(rx[1], 0xFF);
}

int main() {
    RUN(test_initial_ff);
    RUN(test_jedec_id);
    RUN(test_read_status);
    RUN(test_wren);
    RUN(test_page_program);
    RUN(test_flash_and_semantics);
    RUN(test_sector_erase);
    RUN(test_chip_erase);
    RUN(test_read_cmd);
    RUN(test_fast_read);
    RUN(test_device_interface);
    RUN(test_write_without_wren);
    RUN(test_wrdi);
    RUN(test_release_deep_powerdown);
    RUN(test_reset_cmd_clears_state);
    RUN(test_sequential_page_program);
    RUN(test_block_erase_64k);
    RUN(test_split_tx_rx_read);
    TEST_REPORT();
}
