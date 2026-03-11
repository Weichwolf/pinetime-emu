#include "../test.hpp"
#include "device/temp.hpp"

using namespace pt;

void test_initial_temp() {
    TEMP temp;
    ASSERT_EQ(temp.read(0x508, Width::Word), 84u); // 21.0°C
}

void test_start_conversion() {
    TEMP temp;
    temp.write(0x000, Width::Word, 1); // START
    // Not ready yet
    ASSERT_EQ(temp.read(0x100, Width::Word), 0u);
    // Tick past delay (36 cycles)
    for (uint64_t cy = 0; cy <= 40; cy++)
        temp.tick(cy);
    ASSERT_EQ(temp.read(0x100, Width::Word), 1u); // DATARDY
}

void test_stop() {
    TEMP temp;
    temp.write(0x000, Width::Word, 1); // START
    temp.write(0x004, Width::Word, 1); // STOP
    for (uint64_t cy = 0; cy <= 100; cy++)
        temp.tick(cy);
    ASSERT_EQ(temp.read(0x100, Width::Word), 0u); // Never completed
}

void test_irq() {
    TEMP temp;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    temp.set_nvic(&nvic, 12);
    temp.write(0x304, Width::Word, 1); // INTENSET bit 0
    temp.write(0x000, Width::Word, 1); // START
    for (uint64_t cy = 0; cy <= 40; cy++)
        temp.tick(cy);
    ASSERT(nvic.is_pending(12));
}

void test_inten() {
    TEMP temp;
    temp.write(0x304, Width::Word, 0x3);
    ASSERT_EQ(temp.read(0x300, Width::Word), 0x3u);
    temp.write(0x308, Width::Word, 0x1);
    ASSERT_EQ(temp.read(0x300, Width::Word), 0x2u);
}

void test_event_clear() {
    TEMP temp;
    temp.write(0x000, Width::Word, 1);
    for (uint64_t cy = 0; cy <= 40; cy++)
        temp.tick(cy);
    ASSERT_EQ(temp.read(0x100, Width::Word), 1u);
    temp.write(0x100, Width::Word, 0); // Clear event
    ASSERT_EQ(temp.read(0x100, Width::Word), 0u);
}

void test_temp_value_after_conversion() {
    TEMP temp;
    temp.write(0x000, Width::Word, 1);
    for (uint64_t cy = 0; cy <= 40; cy++)
        temp.tick(cy);
    ASSERT_EQ(temp.read(0x100, Width::Word), 1u); // DATARDY
    uint32_t val = temp.read(0x508, Width::Word);
    ASSERT_EQ(val, 84u); // 21.0°C in 0.25°C units
}

void test_repeated_conversions() {
    TEMP temp;
    temp.write(0x000, Width::Word, 1);
    for (uint64_t cy = 0; cy <= 40; cy++)
        temp.tick(cy);
    ASSERT_EQ(temp.read(0x100, Width::Word), 1u);

    // Clear and do another
    temp.write(0x100, Width::Word, 0);
    temp.write(0x000, Width::Word, 1);
    for (uint64_t cy = 41; cy <= 80; cy++)
        temp.tick(cy);
    ASSERT_EQ(temp.read(0x100, Width::Word), 1u);
}

int main() {
    RUN(test_initial_temp);
    RUN(test_start_conversion);
    RUN(test_stop);
    RUN(test_irq);
    RUN(test_inten);
    RUN(test_event_clear);
    RUN(test_temp_value_after_conversion);
    RUN(test_repeated_conversions);
    TEST_REPORT();
}
