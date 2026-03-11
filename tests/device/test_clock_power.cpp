#include "../test.hpp"
#include "device/nvic.hpp"
#include "device/clock_power.hpp"

using namespace pt;

void test_hfclk_start_stop() {
    ClockPower cp;
    // Start HFCLK
    cp.write(0x000, Width::Word, 1);
    ASSERT_EQ(cp.read(0x100, Width::Word), 1u);  // HFCLKSTARTED event
    uint32_t stat = cp.read(0x408, Width::Word);  // HFCLKSTAT
    ASSERT(stat & 1);       // STATE=Running
    ASSERT(stat & 0x10000); // SRC echoed back

    // Stop HFCLK
    cp.write(0x004, Width::Word, 1);
    ASSERT_EQ(cp.read(0x408, Width::Word), 0u); // Stopped
    ASSERT_EQ(cp.read(0x100, Width::Word), 0u); // Event cleared
}

void test_lfclk_start() {
    ClockPower cp;
    cp.write(0x518, Width::Word, 1); // LFCLKSRC = XTAL
    cp.write(0x008, Width::Word, 1); // LFCLKSTART
    ASSERT_EQ(cp.read(0x104, Width::Word), 1u); // LFCLKSTARTED
    uint32_t stat = cp.read(0x418, Width::Word);
    ASSERT(stat & 1);          // Running
    ASSERT(stat & (1 << 16));  // SRC=XTAL
}

void test_resetreas() {
    ClockPower cp;
    ASSERT_EQ(cp.read(0x400, Width::Word), 0u);
    // Write 1 to clear bits (no bits set, just verify no crash)
    cp.write(0x400, Width::Word, 0xFF);
}

void test_ramon() {
    ClockPower cp;
    ASSERT_EQ(cp.read(0x428, Width::Word), 3u); // Default: all RAM on
    cp.write(0x428, Width::Word, 0xF);
    ASSERT_EQ(cp.read(0x428, Width::Word), 0xFu);
}

void test_inten() {
    ClockPower cp;
    cp.write(0x304, Width::Word, 0x3); // INTENSET bits 0,1
    ASSERT_EQ(cp.read(0x300, Width::Word), 0x3u);
    cp.write(0x308, Width::Word, 0x1); // INTENCLR bit 0
    ASSERT_EQ(cp.read(0x300, Width::Word), 0x2u);
}

void test_irq_on_hfclk_start() {
    ClockPower cp;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    cp.set_nvic(&nvic, 0);

    cp.write(0x304, Width::Word, 1); // Enable HFCLKSTARTED interrupt
    cp.write(0x000, Width::Word, 1); // Start HFCLK
    ASSERT(nvic.is_pending(0));
}

void test_lfclk_src_default() {
    ClockPower cp;
    // Default LFCLKSRC should be 0 (RC)
    ASSERT_EQ(cp.read(0x518, Width::Word), 0u);
}

void test_hfclk_src() {
    ClockPower cp;
    cp.write(0x40C, Width::Word, 1); // HFCLKSRC = XTAL
    ASSERT_EQ(cp.read(0x40C, Width::Word), 1u);
}

void test_lfclk_stop() {
    ClockPower cp;
    cp.write(0x008, Width::Word, 1); // LFCLKSTART
    ASSERT_EQ(cp.read(0x418, Width::Word) & 1, 1u); // Running
    cp.write(0x00C, Width::Word, 1); // LFCLKSTOP
    ASSERT_EQ(cp.read(0x418, Width::Word), 0u); // Stopped
}

void test_dcdcen_accepted() {
    ClockPower cp;
    // DCDCEN write should not crash (silently accepted)
    cp.write(0x578, Width::Word, 1);
}

void test_event_clear() {
    ClockPower cp;
    cp.write(0x000, Width::Word, 1); // HFCLKSTART
    ASSERT_EQ(cp.read(0x100, Width::Word), 1u);
    cp.write(0x100, Width::Word, 0);
    ASSERT_EQ(cp.read(0x100, Width::Word), 0u);
}

int main() {
    RUN(test_hfclk_start_stop);
    RUN(test_lfclk_start);
    RUN(test_resetreas);
    RUN(test_ramon);
    RUN(test_inten);
    RUN(test_irq_on_hfclk_start);
    RUN(test_lfclk_src_default);
    RUN(test_hfclk_src);
    RUN(test_lfclk_stop);
    RUN(test_dcdcen_accepted);
    RUN(test_event_clear);
    TEST_REPORT();
}
