#include "../test.hpp"
#include "device/saadc.hpp"
#include "device/memory.hpp"
#include "bus.hpp"

using namespace pt;

void test_initial_state() {
    SAADC saadc;
    ASSERT_EQ(saadc.read(0x500, Width::Word), 0u); // ENABLE
    ASSERT_EQ(saadc.read(0x5F0, Width::Word), 2u); // RESOLUTION=12bit
}

void test_enable() {
    SAADC saadc;
    saadc.write(0x500, Width::Word, 1);
    ASSERT_EQ(saadc.read(0x500, Width::Word), 1u);
}

void test_start_event() {
    SAADC saadc;
    saadc.write(0x000, Width::Word, 1); // START
    ASSERT_EQ(saadc.read(0x100, Width::Word), 1u); // STARTED
}

void test_sample_writes_result() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    SAADC saadc;
    saadc.set_bus(&bus);
    saadc.write(0x500, Width::Word, 1); // ENABLE

    // Configure channel 0
    saadc.write(0x510, Width::Word, 8); // PSELP = AIN7
    saadc.write(0x62C, Width::Word, 0x20000100); // RESULT PTR
    saadc.write(0x630, Width::Word, 1);           // RESULT MAXCNT

    saadc.write(0x004, Width::Word, 1); // SAMPLE

    ASSERT_EQ(saadc.read(0x108, Width::Word), 1u); // DONE
    ASSERT_EQ(saadc.read(0x104, Width::Word), 1u); // END
    ASSERT_EQ(saadc.read(0x634, Width::Word), 1u); // AMOUNT

    uint16_t result = bus.read16(0x20000100);
    ASSERT(result > 0);     // Battery ADC value ~1092
    ASSERT(result < 4096);  // 12-bit max
}

void test_stop() {
    SAADC saadc;
    saadc.write(0x008, Width::Word, 1);
    ASSERT_EQ(saadc.read(0x164, Width::Word), 1u); // STOPPED
}

void test_calibrate() {
    SAADC saadc;
    saadc.write(0x00C, Width::Word, 1);
    ASSERT_EQ(saadc.read(0x160, Width::Word), 1u); // CALIBRATEDONE
}

void test_channel_config() {
    SAADC saadc;
    saadc.write(0x510, Width::Word, 8);        // CH[0].PSELP
    saadc.write(0x518, Width::Word, 0x30000);  // CH[0].CONFIG
    ASSERT_EQ(saadc.read(0x510, Width::Word), 8u);
    ASSERT_EQ(saadc.read(0x518, Width::Word), 0x30000u);
}

void test_resolution() {
    SAADC saadc;
    saadc.write(0x5F0, Width::Word, 3); // 14-bit
    ASSERT_EQ(saadc.read(0x5F0, Width::Word), 3u);
}

void test_irq() {
    SAADC saadc;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    saadc.set_nvic(&nvic, 7);

    saadc.write(0x304, Width::Word, 1u << 0); // INTENSET STARTED
    saadc.write(0x000, Width::Word, 1);        // START
    ASSERT(nvic.is_pending(7));
}

void test_inten() {
    SAADC saadc;
    saadc.write(0x304, Width::Word, 0xFF);
    ASSERT_EQ(saadc.read(0x300, Width::Word), 0xFFu);
    saadc.write(0x308, Width::Word, 0x0F);
    ASSERT_EQ(saadc.read(0x300, Width::Word), 0xF0u);
}

void test_event_clear() {
    SAADC saadc;
    saadc.write(0x000, Width::Word, 1);
    ASSERT_EQ(saadc.read(0x100, Width::Word), 1u);
    saadc.write(0x100, Width::Word, 0);
    ASSERT_EQ(saadc.read(0x100, Width::Word), 0u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_enable);
    RUN(test_start_event);
    RUN(test_sample_writes_result);
    RUN(test_stop);
    RUN(test_calibrate);
    RUN(test_channel_config);
    RUN(test_resolution);
    RUN(test_irq);
    RUN(test_inten);
    RUN(test_event_clear);
    TEST_REPORT();
}
