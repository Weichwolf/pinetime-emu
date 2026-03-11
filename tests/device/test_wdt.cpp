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
    wdt.write(0x508, Width::Word, 3); // Enable RR[0] and RR[1]
    wdt.write(0x000, Width::Word, 1); // START required for RR
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

void test_reload_any_value_accepted() {
    WDT wdt;
    wdt.write(0x000, Width::Word, 1); // START required
    // Emulator accepts any write to RR[] (no key check)
    wdt.write(0x600, Width::Word, 0xDEADBEEF);
    ASSERT(wdt.read(0x404, Width::Word) & 1); // REQSTATUS set
}

void test_all_reload_channels() {
    WDT wdt;
    wdt.write(0x508, Width::Word, 0xFF); // Enable all 8 RR channels
    wdt.write(0x000, Width::Word, 1);    // START required
    for (int i = 0; i < 8; i++) {
        wdt.write(0x600 + i*4, Width::Word, 0x6E524635);
        ASSERT(wdt.read(0x404, Width::Word) & (1u << i));
    }
}

void test_timeout() {
    WDT wdt;
    NVIC nvic;
    wdt.set_nvic(&nvic, 16);
    wdt.write(0x504, Width::Word, 99); // CRV=99 → timeout after 100*1953=195300 cycles
    wdt.write(0x304, Width::Word, 1);  // INTENSET bit 0
    wdt.write(0x000, Width::Word, 1);  // START

    // First tick initializes last_reload_
    auto r = wdt.tick(1000);
    ASSERT(!r.has_value());

    // Before timeout: no event
    r = wdt.tick(1000 + 195299);
    ASSERT(!r.has_value());
    ASSERT_EQ(wdt.read(0x100, Width::Word), 0u); // No TIMEOUT event yet

    // At timeout: event fires
    r = wdt.tick(1000 + 195300);
    ASSERT(r.has_value());
    ASSERT_EQ(r->irq, 16u);
    ASSERT_EQ(wdt.read(0x100, Width::Word), 1u); // TIMEOUT event
}

void test_reload_resets_countdown() {
    WDT wdt;
    wdt.write(0x504, Width::Word, 99); // CRV=99
    wdt.write(0x000, Width::Word, 1);
    wdt.tick(1000); // Initialize

    // Advance halfway
    wdt.tick(1000 + 100000);
    ASSERT_EQ(wdt.read(0x100, Width::Word), 0u); // Not timed out

    // Reload — resets countdown
    wdt.write(0x600, Width::Word, 0x6E524635);

    // Re-init after reload
    wdt.tick(200000);

    // Should NOT timeout at original deadline
    auto r = wdt.tick(200000 + 195299);
    ASSERT(!r.has_value());
}

void test_next_event() {
    WDT wdt;
    wdt.write(0x504, Width::Word, 99); // CRV=99
    ASSERT_EQ(wdt.next_event(0), UINT64_MAX); // Not started

    wdt.write(0x000, Width::Word, 1);
    ASSERT_EQ(wdt.next_event(0), UINT64_MAX); // Started but last_reload_=0

    wdt.tick(1000); // Initialize last_reload_=1000
    uint64_t ev = wdt.next_event(1000);
    ASSERT_EQ(ev, 1000 + 195300); // deadline = 1000 + (99+1)*1953
}

void test_crv_locked_after_start() {
    WDT wdt;
    wdt.write(0x504, Width::Word, 0x1234);
    ASSERT_EQ(wdt.read(0x504, Width::Word), 0x1234u);
    wdt.write(0x000, Width::Word, 1); // START
    wdt.write(0x504, Width::Word, 0x5678); // Should be ignored
    ASSERT_EQ(wdt.read(0x504, Width::Word), 0x1234u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_start);
    RUN(test_crv);
    RUN(test_reload_request);
    RUN(test_inten);
    RUN(test_rren_config);
    RUN(test_reload_any_value_accepted);
    RUN(test_all_reload_channels);
    RUN(test_timeout);
    RUN(test_reload_resets_countdown);
    RUN(test_next_event);
    RUN(test_crv_locked_after_start);
    TEST_REPORT();
}
