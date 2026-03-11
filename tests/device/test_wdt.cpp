#include "../test.hpp"
#include "device/wdt.hpp"

using namespace pt;

void test_initial_state() {
    WDT wdt;
    ASSERT_EQ(wdt.read(0x400, Width::Word), 0u); // RUNSTATUS
    ASSERT_EQ(wdt.read(0x504, Width::Word), 0xFFFFFFFF); // CRV default
    ASSERT_EQ(wdt.read(0x508, Width::Word), 1u); // RREN default
    ASSERT_EQ(wdt.read(0x50C, Width::Word), 1u); // CONFIG default
}

void test_start() {
    WDT wdt;
    wdt.write(0x000, Width::Word, 1);
    ASSERT_EQ(wdt.read(0x400, Width::Word), 1u); // RUNSTATUS = 1
}

void test_crv() {
    WDT wdt;
    wdt.write(0x504, Width::Word, 0x12345678);
    ASSERT_EQ(wdt.read(0x504, Width::Word), 0x12345678u);
}

void test_reload_request() {
    WDT wdt;
    wdt.write(0x600, Width::Word, 0x6E524635); // RR[0]
    ASSERT(wdt.read(0x404, Width::Word) & 1); // REQSTATUS bit 0
    wdt.write(0x604, Width::Word, 0x6E524635); // RR[1]
    ASSERT(wdt.read(0x404, Width::Word) & 2); // REQSTATUS bit 1
}

void test_inten() {
    WDT wdt;
    wdt.write(0x304, Width::Word, 0x3);
    ASSERT_EQ(wdt.read(0x300, Width::Word), 0x3u);
    wdt.write(0x308, Width::Word, 0x1);
    ASSERT_EQ(wdt.read(0x300, Width::Word), 0x2u);
}

void test_rren_config() {
    WDT wdt;
    wdt.write(0x508, Width::Word, 0xFF);
    ASSERT_EQ(wdt.read(0x508, Width::Word), 0xFFu);
    wdt.write(0x50C, Width::Word, 0x09);
    ASSERT_EQ(wdt.read(0x50C, Width::Word), 0x09u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_start);
    RUN(test_crv);
    RUN(test_reload_request);
    RUN(test_inten);
    RUN(test_rren_config);
    TEST_REPORT();
}
