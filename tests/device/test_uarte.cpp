#include "../test.hpp"
#include "device/uarte.hpp"
#include "bus.hpp"
#include "device/nvic.hpp"
#include <string>

using namespace pt;

// Minimal bus with SRAM for DMA
static uint8_t sram[1024];
static Bus make_bus() {
    Bus b;
    b.set_fast_path(nullptr, 0, sram, 0x20000000, sizeof(sram));
    return b;
}

void test_initial_state() {
    UARTE u;
    ASSERT_EQ(u.read(0x500, Width::Word), 0u); // disabled
    ASSERT_EQ(u.read(0x120, Width::Word), 0u); // ENDTX
    ASSERT_EQ(u.read(0x150, Width::Word), 0u); // TXSTARTED
}

void test_enable() {
    UARTE u;
    u.write(0x500, Width::Word, 8);
    ASSERT_EQ(u.read(0x500, Width::Word), 8u);
    u.write(0x500, Width::Word, 0);
    ASSERT_EQ(u.read(0x500, Width::Word), 0u);
}

void test_tx_dma() {
    Bus bus = make_bus();
    UARTE u;
    NVIC nvic;
    u.set_bus(&bus);
    u.set_nvic(&nvic, 2);

    // Write "Hi" to SRAM
    sram[0] = 'H'; sram[1] = 'i';

    // Configure TX DMA
    u.write(0x544, Width::Word, 0x20000000); // TXD.PTR
    u.write(0x548, Width::Word, 2);           // TXD.MAXCNT
    u.write(0x500, Width::Word, 8);           // Enable

    // Capture output
    std::string captured;
    u.set_tx_callback([&](const char* d, size_t n) { captured.assign(d, n); });

    // Start TX
    u.write(0x008, Width::Word, 1);

    ASSERT(captured == "Hi");
    ASSERT_EQ(u.read(0x54C, Width::Word), 2u); // TXD.AMOUNT
    ASSERT_EQ(u.read(0x120, Width::Word), 1u); // ENDTX
    ASSERT_EQ(u.read(0x150, Width::Word), 1u); // TXSTARTED
}

void test_tx_disabled_noop() {
    Bus bus = make_bus();
    UARTE u;
    u.set_bus(&bus);

    std::string captured;
    u.set_tx_callback([&](const char* d, size_t n) { captured.assign(d, n); });

    u.write(0x544, Width::Word, 0x20000000);
    u.write(0x548, Width::Word, 2);
    // Don't enable — TX should be a no-op
    u.write(0x008, Width::Word, 1);
    ASSERT(captured.empty());
}

void test_inten() {
    UARTE u;
    u.write(0x304, Width::Word, 0xFF); // INTENSET
    ASSERT_EQ(u.read(0x300, Width::Word), 0xFFu);
    u.write(0x308, Width::Word, 0x0F); // INTENCLR
    ASSERT_EQ(u.read(0x300, Width::Word), 0xF0u);
}

void test_psel() {
    UARTE u;
    u.write(0x50C, Width::Word, 6); // PSEL.TXD
    ASSERT_EQ(u.read(0x50C, Width::Word), 6u);
    u.write(0x514, Width::Word, 8); // PSEL.RXD
    ASSERT_EQ(u.read(0x514, Width::Word), 8u);
}

void test_startrx() {
    UARTE u;
    NVIC nvic;
    u.set_nvic(&nvic, 2);
    u.write(0x500, Width::Word, 8); // Enable
    u.write(0x304, Width::Word, (1u << 19)); // INTENSET RXSTARTED
    u.write(0x000, Width::Word, 1); // STARTRX
    ASSERT_EQ(u.read(0x148, Width::Word), 1u); // RXSTARTED
}

void test_stoprx() {
    UARTE u;
    u.write(0x004, Width::Word, 1); // STOPRX
    ASSERT_EQ(u.read(0x144, Width::Word), 1u); // RXTO
    ASSERT_EQ(u.read(0x110, Width::Word), 1u); // ENDRX
}

void test_event_clear() {
    UARTE u;
    Bus bus = make_bus();
    u.set_bus(&bus);
    u.write(0x500, Width::Word, 8);
    sram[0] = 'x';
    u.write(0x544, Width::Word, 0x20000000);
    u.write(0x548, Width::Word, 1);
    u.set_tx_callback([](const char*, size_t) {});
    u.write(0x008, Width::Word, 1);
    ASSERT_EQ(u.read(0x120, Width::Word), 1u); // ENDTX set
    u.write(0x120, Width::Word, 0); // Clear
    ASSERT_EQ(u.read(0x120, Width::Word), 0u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_enable);
    RUN(test_tx_dma);
    RUN(test_tx_disabled_noop);
    RUN(test_inten);
    RUN(test_psel);
    RUN(test_startrx);
    RUN(test_stoprx);
    RUN(test_event_clear);
    TEST_REPORT();
}
