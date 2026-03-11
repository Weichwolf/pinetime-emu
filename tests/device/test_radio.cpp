#include "../test.hpp"
#include "device/radio.hpp"

using namespace pt;

void test_initial_state() {
    Radio radio;
    ASSERT_EQ(radio.read(0x550, Width::Word), 0u); // STATE=Disabled
    ASSERT_EQ(radio.read(0xFFC, Width::Word), 1u); // POWER=1
}

void test_txen_ready() {
    Radio radio;
    radio.write(0x000, Width::Word, 1); // TXEN
    ASSERT_EQ(radio.read(0x100, Width::Word), 1u); // READY event
    ASSERT_EQ(radio.read(0x550, Width::Word), 11u); // TXIDLE state
}

void test_rxen_ready() {
    Radio radio;
    radio.write(0x004, Width::Word, 1); // RXEN
    ASSERT_EQ(radio.read(0x100, Width::Word), 1u); // READY event
    ASSERT_EQ(radio.read(0x550, Width::Word), 3u); // RXIDLE
}

void test_tx_complete() {
    Radio radio;
    radio.write(0x000, Width::Word, 1); // TXEN
    radio.write(0x008, Width::Word, 1); // START
    ASSERT_EQ(radio.read(0x104, Width::Word), 1u); // ADDRESS
    ASSERT_EQ(radio.read(0x108, Width::Word), 1u); // PAYLOAD
    ASSERT_EQ(radio.read(0x10C, Width::Word), 1u); // END
}

void test_disable() {
    Radio radio;
    radio.write(0x000, Width::Word, 1); // TXEN
    radio.write(0x010, Width::Word, 1); // DISABLE
    ASSERT_EQ(radio.read(0x110, Width::Word), 1u); // DISABLED event
    ASSERT_EQ(radio.read(0x550, Width::Word), 0u); // STATE=Disabled
}

void test_shorts_ready_start() {
    Radio radio;
    radio.write(0x200, Width::Word, 1u << 0); // READY_START
    radio.write(0x000, Width::Word, 1);        // TXEN -> READY -> auto START
    ASSERT_EQ(radio.read(0x10C, Width::Word), 1u); // END (TX completed)
}

void test_shorts_end_disable() {
    Radio radio;
    radio.write(0x200, Width::Word, (1u << 0) | (1u << 1)); // READY_START + END_DISABLE
    radio.write(0x000, Width::Word, 1); // TXEN -> READY -> START -> END -> DISABLE
    ASSERT_EQ(radio.read(0x110, Width::Word), 1u); // DISABLED
    ASSERT_EQ(radio.read(0x550, Width::Word), 0u);
}

void test_config_registers() {
    Radio radio;
    radio.write(0x504, Width::Word, 0x20000100); // PACKETPTR
    ASSERT_EQ(radio.read(0x504, Width::Word), 0x20000100u);
    radio.write(0x508, Width::Word, 37); // FREQUENCY
    ASSERT_EQ(radio.read(0x508, Width::Word), 37u);
    radio.write(0x510, Width::Word, 3); // MODE
    ASSERT_EQ(radio.read(0x510, Width::Word), 3u);
    radio.write(0x534, Width::Word, 0x30002); // CRCCNF
    ASSERT_EQ(radio.read(0x534, Width::Word), 0x30002u);
}

void test_irq() {
    Radio radio;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    radio.set_nvic(&nvic, 1);

    radio.write(0x304, Width::Word, 1u << 0); // INTENSET READY
    radio.write(0x000, Width::Word, 1);        // TXEN -> READY
    ASSERT(nvic.is_pending(1));
}

void test_dab_dap() {
    Radio radio;
    radio.write(0x600, Width::Word, 0x11223344); // DAB[0]
    ASSERT_EQ(radio.read(0x600, Width::Word), 0x11223344u);
    radio.write(0x620, Width::Word, 0x5566);     // DAP[0]
    ASSERT_EQ(radio.read(0x620, Width::Word), 0x5566u);
}

void test_event_clear() {
    Radio radio;
    radio.write(0x000, Width::Word, 1); // TXEN -> READY
    ASSERT_EQ(radio.read(0x100, Width::Word), 1u);
    radio.write(0x100, Width::Word, 0); // Clear
    ASSERT_EQ(radio.read(0x100, Width::Word), 0u);
}

void test_inten() {
    Radio radio;
    radio.write(0x304, Width::Word, 0x1F);
    ASSERT_EQ(radio.read(0x300, Width::Word), 0x1Fu);
    radio.write(0x308, Width::Word, 0x0F);
    ASSERT_EQ(radio.read(0x300, Width::Word), 0x10u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_txen_ready);
    RUN(test_rxen_ready);
    RUN(test_tx_complete);
    RUN(test_disable);
    RUN(test_shorts_ready_start);
    RUN(test_shorts_end_disable);
    RUN(test_config_registers);
    RUN(test_irq);
    RUN(test_dab_dap);
    RUN(test_event_clear);
    RUN(test_inten);
    TEST_REPORT();
}
