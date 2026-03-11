#include "../test.hpp"
#include "device/spim.hpp"
#include "device/memory.hpp"
#include "bus.hpp"

using namespace pt;

void test_initial_state() {
    SPIM spim;
    ASSERT_EQ(spim.read(0x500, Width::Word), 0u); // ENABLE
    ASSERT_EQ(spim.read(0x5C0, Width::Word), 0xFFu); // ORC default
}

void test_pin_config() {
    SPIM spim;
    spim.write(0x508, Width::Word, 25); // SCK
    spim.write(0x50C, Width::Word, 26); // MOSI
    spim.write(0x510, Width::Word, 27); // MISO
    ASSERT_EQ(spim.read(0x508, Width::Word), 25u);
    ASSERT_EQ(spim.read(0x50C, Width::Word), 26u);
    ASSERT_EQ(spim.read(0x510, Width::Word), 27u);
}

void test_transfer() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    SPIM spim;
    spim.set_bus(&bus);

    bool called = false;
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len,
                          uint8_t* rx, uint32_t rx_len) {
        called = true;
        ASSERT_EQ(tx_len, 3u);
        ASSERT_EQ(tx[0], 0xAA);
        ASSERT_EQ(tx[1], 0xBB);
        ASSERT_EQ(tx[2], 0xCC);
        for (uint32_t i = 0; i < rx_len; i++)
            rx[i] = 0x42;
    });

    // Write TX data to RAM
    bus.write8(0x20000000, 0xAA);
    bus.write8(0x20000001, 0xBB);
    bus.write8(0x20000002, 0xCC);

    spim.write(0x500, Width::Word, 7);           // ENABLE=7 (SPIM)
    spim.write(0x544, Width::Word, 0x20000000);  // TXD.PTR
    spim.write(0x548, Width::Word, 3);           // TXD.MAXCNT
    spim.write(0x534, Width::Word, 0x20000100);  // RXD.PTR
    spim.write(0x538, Width::Word, 2);           // RXD.MAXCNT
    spim.write(0x010, Width::Word, 1);           // START

    ASSERT(called);
    ASSERT_EQ(spim.read(0x118, Width::Word), 1u); // END event
    ASSERT_EQ(spim.read(0x54C, Width::Word), 3u); // TXD.AMOUNT
    ASSERT_EQ(spim.read(0x53C, Width::Word), 2u); // RXD.AMOUNT
    ASSERT_EQ(bus.read8(0x20000100), 0x42);
    ASSERT_EQ(bus.read8(0x20000101), 0x42);
}

void test_transfer_not_enabled() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    SPIM spim;
    spim.set_bus(&bus);

    bool called = false;
    spim.set_callback([&](const uint8_t*, uint32_t, uint8_t*, uint32_t) {
        called = true;
    });

    spim.write(0x500, Width::Word, 1); // Not SPIM mode (7)
    spim.write(0x010, Width::Word, 1);
    ASSERT(!called);
}

void test_stop() {
    SPIM spim;
    spim.write(0x014, Width::Word, 1); // STOP
    ASSERT_EQ(spim.read(0x104, Width::Word), 1u); // STOPPED
}

void test_irq() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    SPIM spim;
    spim.set_bus(&bus);
    spim.set_nvic(&nvic, 3);
    spim.set_callback([](const uint8_t*, uint32_t, uint8_t*, uint32_t) {});

    spim.write(0x304, Width::Word, 1u << 6); // INTENSET END
    spim.write(0x500, Width::Word, 7);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 1);
    spim.write(0x534, Width::Word, 0x20000010);
    spim.write(0x538, Width::Word, 0);
    spim.write(0x010, Width::Word, 1);

    ASSERT(nvic.is_pending(3));
}

void test_legacy_spi() {
    SPIM spim;
    bool called = false;
    spim.set_callback([&](const uint8_t* tx, uint32_t, uint8_t* rx, uint32_t) {
        called = true;
        rx[0] = tx[0] ^ 0xFF;
    });

    spim.write(0x51C, Width::Word, 0xAA); // Legacy TXD
    ASSERT(called);
    ASSERT_EQ(spim.read(0x108, Width::Word), 1u); // READY (legacy)
    ASSERT_EQ(spim.read(0x518, Width::Word), 0x55u); // RXD FIFO
}

void test_inten() {
    SPIM spim;
    spim.write(0x304, Width::Word, 0xFF);
    ASSERT_EQ(spim.read(0x300, Width::Word), 0xFFu);
    spim.write(0x308, Width::Word, 0x0F);
    ASSERT_EQ(spim.read(0x300, Width::Word), 0xF0u);
}

void test_transfer_count() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    SPIM spim;
    spim.set_bus(&bus);
    spim.set_callback([](const uint8_t*, uint32_t, uint8_t*, uint32_t) {});

    ASSERT_EQ(spim.transfer_count(), 0u);
    spim.write(0x500, Width::Word, 7);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 1);
    spim.write(0x534, Width::Word, 0x20000010);
    spim.write(0x538, Width::Word, 0);
    spim.write(0x010, Width::Word, 1);
    ASSERT_EQ(spim.transfer_count(), 1u);
}

void test_zero_length_transfer() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    SPIM spim;
    spim.set_bus(&bus);

    bool called = false;
    spim.set_callback([&](const uint8_t*, uint32_t tx_len, uint8_t*, uint32_t rx_len) {
        called = true;
        ASSERT_EQ(tx_len, 0u);
        ASSERT_EQ(rx_len, 0u);
    });

    spim.write(0x500, Width::Word, 7);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 0); // TX len=0
    spim.write(0x534, Width::Word, 0x20000010);
    spim.write(0x538, Width::Word, 0); // RX len=0
    spim.write(0x010, Width::Word, 1);

    ASSERT(called);
    ASSERT_EQ(spim.read(0x118, Width::Word), 1u); // END
}

void test_event_clear() {
    SPIM spim;
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    spim.set_bus(&bus);
    spim.set_callback([](const uint8_t*, uint32_t, uint8_t*, uint32_t) {});

    spim.write(0x500, Width::Word, 7);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 1);
    spim.write(0x534, Width::Word, 0x20000010);
    spim.write(0x538, Width::Word, 0);
    spim.write(0x010, Width::Word, 1);

    ASSERT_EQ(spim.read(0x118, Width::Word), 1u);
    spim.write(0x118, Width::Word, 0);
    ASSERT_EQ(spim.read(0x118, Width::Word), 0u);
}

void test_frequency() {
    SPIM spim;
    spim.write(0x524, Width::Word, 0x80000000); // 8 Mbps
    ASSERT_EQ(spim.read(0x524, Width::Word), 0x80000000u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_pin_config);
    RUN(test_transfer);
    RUN(test_transfer_not_enabled);
    RUN(test_stop);
    RUN(test_irq);
    RUN(test_legacy_spi);
    RUN(test_inten);
    RUN(test_transfer_count);
    RUN(test_zero_length_transfer);
    RUN(test_event_clear);
    RUN(test_frequency);
    TEST_REPORT();
}
