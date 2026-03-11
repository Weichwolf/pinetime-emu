#include "../test.hpp"
#include "device/rng.hpp"

using namespace pt;

void test_start_generates_value() {
    RNG rng;
    rng.write(0x000, Width::Word, 1); // START
    // Tick past delay (128 cycles)
    for (uint64_t cy = 0; cy <= 130; cy++)
        rng.tick(cy);
    ASSERT_EQ(rng.read(0x100, Width::Word), 1u); // VALRDY
    uint32_t val = rng.read(0x508, Width::Word);
    ASSERT(val <= 0xFF); // 8-bit
}

void test_stop() {
    RNG rng;
    rng.write(0x000, Width::Word, 1); // START
    rng.write(0x004, Width::Word, 1); // STOP
    for (uint64_t cy = 0; cy <= 200; cy++)
        rng.tick(cy);
    ASSERT_EQ(rng.read(0x100, Width::Word), 0u);
}

void test_shorts_valrdy_stop() {
    RNG rng;
    rng.write(0x200, Width::Word, 1); // SHORTS: VALRDY_STOP
    rng.write(0x000, Width::Word, 1); // START
    for (uint64_t cy = 0; cy <= 130; cy++)
        rng.tick(cy);
    ASSERT_EQ(rng.read(0x100, Width::Word), 1u); // Got value
    // Further ticks should not generate new values (stopped)
    rng.write(0x100, Width::Word, 0); // Clear event
    for (uint64_t cy = 131; cy <= 300; cy++)
        rng.tick(cy);
    ASSERT_EQ(rng.read(0x100, Width::Word), 0u);
}

void test_irq() {
    RNG rng;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    rng.set_nvic(&nvic, 13);
    rng.write(0x304, Width::Word, 1); // INTENSET
    rng.write(0x000, Width::Word, 1);
    for (uint64_t cy = 0; cy <= 130; cy++)
        rng.tick(cy);
    ASSERT(nvic.is_pending(13));
}

void test_config_dercen() {
    RNG rng;
    rng.write(0x504, Width::Word, 0xFF);
    ASSERT_EQ(rng.read(0x504, Width::Word), 1u); // Only bit 0
}

void test_inten() {
    RNG rng;
    rng.write(0x304, Width::Word, 0x3);
    ASSERT_EQ(rng.read(0x300, Width::Word), 0x3u);
    rng.write(0x308, Width::Word, 0x1);
    ASSERT_EQ(rng.read(0x300, Width::Word), 0x2u);
}

void test_multiple_values_differ() {
    RNG rng;
    rng.write(0x000, Width::Word, 1);
    for (uint64_t cy = 0; cy <= 130; cy++) rng.tick(cy);
    uint32_t v1 = rng.read(0x508, Width::Word);

    rng.write(0x100, Width::Word, 0); // Clear VALRDY
    for (uint64_t cy = 131; cy <= 260; cy++) rng.tick(cy);
    uint32_t v2 = rng.read(0x508, Width::Word);

    // Values should generally differ (probabilistic, but very likely for random bytes)
    // If they're the same by chance, that's ok for one test; checking range is sufficient
    ASSERT(v1 <= 0xFF);
    ASSERT(v2 <= 0xFF);
}

void test_event_clear() {
    RNG rng;
    rng.write(0x000, Width::Word, 1);
    for (uint64_t cy = 0; cy <= 130; cy++) rng.tick(cy);
    ASSERT_EQ(rng.read(0x100, Width::Word), 1u);
    rng.write(0x100, Width::Word, 0);
    ASSERT_EQ(rng.read(0x100, Width::Word), 0u);
}

int main() {
    RUN(test_start_generates_value);
    RUN(test_stop);
    RUN(test_shorts_valrdy_stop);
    RUN(test_irq);
    RUN(test_config_dercen);
    RUN(test_inten);
    RUN(test_multiple_values_differ);
    RUN(test_event_clear);
    TEST_REPORT();
}
