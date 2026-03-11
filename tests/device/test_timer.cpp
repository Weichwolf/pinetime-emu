#include "../test.hpp"
#include "device/timer.hpp"

using namespace pt;

// --- Timer ---

void test_timer_start_stop() {
    Timer t;
    t.write(0x000, Width::Word, 1); // START
    // Timer is running, but needs tick() to advance
    t.write(0x004, Width::Word, 1); // STOP
}

void test_timer_clear() {
    Timer t;
    t.write(0x000, Width::Word, 1); // START
    t.write(0x00C, Width::Word, 1); // CLEAR
    t.write(0x040, Width::Word, 1); // CAPTURE[0]
    ASSERT_EQ(t.read(0x540, Width::Word), 0u); // CC[0] = 0
}

void test_timer_counter_mode() {
    Timer t;
    t.write(0x504, Width::Word, 1); // MODE=Counter
    t.write(0x000, Width::Word, 1); // START
    t.write(0x008, Width::Word, 1); // COUNT
    t.write(0x008, Width::Word, 1); // COUNT
    t.write(0x008, Width::Word, 1); // COUNT
    t.write(0x040, Width::Word, 1); // CAPTURE[0]
    ASSERT_EQ(t.read(0x540, Width::Word), 3u);
}

void test_timer_bitmode() {
    Timer t;
    t.write(0x504, Width::Word, 1); // MODE=Counter
    t.write(0x508, Width::Word, 1); // BITMODE=8bit
    t.write(0x000, Width::Word, 1);
    // Count 256 times -> wraps at 8-bit
    for (int i = 0; i < 256; i++)
        t.write(0x008, Width::Word, 1);
    t.write(0x040, Width::Word, 1); // CAPTURE[0]
    ASSERT_EQ(t.read(0x540, Width::Word), 0u); // Wrapped
}

void test_timer_compare_event() {
    Timer t;
    NVIC nvic;
    t.set_nvic(&nvic, 8);
    t.write(0x540, Width::Word, 5); // CC[0] = 5
    t.write(0x510, Width::Word, 0); // PRESCALER=0: 16MHz, tick every 4 CPU cycles
    t.write(0x508, Width::Word, 3); // BITMODE=32bit
    t.write(0x304, Width::Word, 1u << 16); // INTENSET COMPARE0
    t.write(0x000, Width::Word, 1); // START

    // Tick 5 timer ticks = 20 CPU cycles at prescaler 0
    for (uint64_t cy = 0; cy <= 20; cy++)
        t.tick(cy);
    ASSERT_EQ(t.read(0x140, Width::Word), 1u); // EVENTS_COMPARE[0]
}

void test_timer_shorts_compare_clear() {
    Timer t;
    t.write(0x540, Width::Word, 3); // CC[0] = 3
    t.write(0x200, Width::Word, 1); // SHORTS: COMPARE0_CLEAR
    t.write(0x510, Width::Word, 0);
    t.write(0x508, Width::Word, 3);
    t.write(0x000, Width::Word, 1);

    // Run enough cycles for counter to reach 3 and clear
    for (uint64_t cy = 0; cy <= 16; cy++)
        t.tick(cy);
    t.write(0x040, Width::Word, 1); // CAPTURE[0]
    uint32_t cc0 = t.read(0x540, Width::Word);
    ASSERT(cc0 < 3); // Counter was cleared after match
}

void test_timer_prescaler() {
    Timer t;
    t.write(0x510, Width::Word, 4); // PRESCALER=4: 1MHz, tick every 64 CPU cycles
    ASSERT_EQ(t.read(0x510, Width::Word), 4u);
}

void test_timer_cc_read_write() {
    Timer t;
    for (int i = 0; i < 6; i++) {
        t.write(0x540 + i*4, Width::Word, 100 + i);
        ASSERT_EQ(t.read(0x540 + i*4, Width::Word), 100u + i);
    }
}

// --- RTC ---

void test_rtc_start_stop() {
    RTC rtc;
    rtc.write(0x000, Width::Word, 1); // START
    rtc.write(0x004, Width::Word, 1); // STOP
}

void test_rtc_prescaler() {
    RTC rtc;
    rtc.write(0x508, Width::Word, 0xFFF);
    ASSERT_EQ(rtc.read(0x508, Width::Word), 0xFFFu);
    rtc.write(0x508, Width::Word, 0x1FFFF); // Mask to 12 bits
    ASSERT_EQ(rtc.read(0x508, Width::Word), 0xFFFu);
}

void test_rtc_counter() {
    RTC rtc;
    rtc.write(0x508, Width::Word, 0); // PRESCALER=0
    rtc.write(0x000, Width::Word, 1); // START

    // 32768 Hz, prescaler 0+1=1, divisor = 1953 CPU cycles
    for (uint64_t cy = 0; cy < 1953 * 3; cy++)
        rtc.tick(cy);
    uint32_t counter = rtc.read(0x504, Width::Word);
    ASSERT(counter > 0);
}

void test_rtc_clear() {
    RTC rtc;
    rtc.write(0x508, Width::Word, 0);
    rtc.write(0x000, Width::Word, 1);
    for (uint64_t cy = 0; cy < 10000; cy++)
        rtc.tick(cy);
    rtc.write(0x008, Width::Word, 1); // CLEAR
    ASSERT_EQ(rtc.read(0x504, Width::Word), 0u);
}

void test_rtc_compare_event() {
    RTC rtc;
    NVIC nvic;
    rtc.set_nvic(&nvic, 17);
    rtc.write(0x508, Width::Word, 0); // PRESCALER=0
    rtc.write(0x540, Width::Word, 2); // CC[0] = 2
    rtc.write(0x304, Width::Word, 1u << 16); // INTENSET COMPARE0
    rtc.write(0x000, Width::Word, 1);

    for (uint64_t cy = 0; cy < 1953 * 3; cy++)
        rtc.tick(cy);
    ASSERT_EQ(rtc.read(0x140, Width::Word), 1u); // EVENTS_COMPARE[0]
}

void test_rtc_evten() {
    RTC rtc;
    rtc.write(0x344, Width::Word, 0xFF); // EVTENSET
    ASSERT_EQ(rtc.read(0x340, Width::Word), 0xFFu);
    rtc.write(0x348, Width::Word, 0x0F); // EVTENCLR
    ASSERT_EQ(rtc.read(0x340, Width::Word), 0xF0u);
}

void test_rtc_inten() {
    RTC rtc;
    rtc.write(0x304, Width::Word, 0xFF);
    ASSERT_EQ(rtc.read(0x300, Width::Word), 0xFFu);
    rtc.write(0x308, Width::Word, 0x0F);
    ASSERT_EQ(rtc.read(0x300, Width::Word), 0xF0u);
}

int main() {
    RUN(test_timer_start_stop);
    RUN(test_timer_clear);
    RUN(test_timer_counter_mode);
    RUN(test_timer_bitmode);
    RUN(test_timer_compare_event);
    RUN(test_timer_shorts_compare_clear);
    RUN(test_timer_prescaler);
    RUN(test_timer_cc_read_write);
    RUN(test_rtc_start_stop);
    RUN(test_rtc_prescaler);
    RUN(test_rtc_counter);
    RUN(test_rtc_clear);
    RUN(test_rtc_compare_event);
    RUN(test_rtc_evten);
    RUN(test_rtc_inten);
    TEST_REPORT();
}
