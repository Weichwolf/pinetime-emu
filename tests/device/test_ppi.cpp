#include "../test.hpp"
#include "device/ppi.hpp"
#include "device/memory.hpp"
#include "bus.hpp"

using namespace pt;

void test_chen() {
    PPI ppi;
    ppi.write(0x500, Width::Word, 0xFF);
    ASSERT_EQ(ppi.read(0x500, Width::Word), 0xFFu);
    ppi.write(0x504, Width::Word, 0x100); // CHENSET
    ASSERT_EQ(ppi.read(0x500, Width::Word), 0x1FFu);
    ppi.write(0x508, Width::Word, 0x0F);  // CHENCLR
    ASSERT_EQ(ppi.read(0x500, Width::Word), 0x1F0u);
}

void test_channel_config() {
    PPI ppi;
    // CH[0].EEP at 0x510, CH[0].TEP at 0x514
    ppi.write(0x510, Width::Word, 0x40008140); // EEP = TIMER0 EVENTS_COMPARE[0]
    ppi.write(0x514, Width::Word, 0x40009000); // TEP = TIMER1 TASKS_START
    ASSERT_EQ(ppi.read(0x510, Width::Word), 0x40008140u);
    ASSERT_EQ(ppi.read(0x514, Width::Word), 0x40009000u);
}

void test_channel_groups() {
    PPI ppi;
    ppi.write(0x800, Width::Word, 0x0F); // CHG[0]
    ASSERT_EQ(ppi.read(0x800, Width::Word), 0x0Fu);
    // TASKS_CHG[0].EN at 0x000
    ppi.write(0x000, Width::Word, 1);
    ASSERT_EQ(ppi.read(0x500, Width::Word) & 0x0F, 0x0Fu); // CHEN |= CHG[0]
    // TASKS_CHG[0].DIS at 0x004
    ppi.write(0x004, Width::Word, 1);
    ASSERT_EQ(ppi.read(0x500, Width::Word) & 0x0F, 0u);
}

void test_fork_tep() {
    PPI ppi;
    ppi.write(0x910, Width::Word, 0x40001000); // FORK_TEP[0]
    ASSERT_EQ(ppi.read(0x910, Width::Word), 0x40001000u);
}

void test_tick_event_routing() {
    // Setup: event at RAM addr, task at another RAM addr
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    PPI ppi;
    ppi.set_bus(&bus);

    // Event register at 0x20000100, task register at 0x20000200
    uint32_t ev_addr = 0x20000100;
    uint32_t task_addr = 0x20000200;

    ppi.write(0x510, Width::Word, ev_addr);  // CH[0].EEP
    ppi.write(0x514, Width::Word, task_addr); // CH[0].TEP
    ppi.write(0x500, Width::Word, 1);         // Enable CH 0

    // No event yet
    ppi.tick(0);
    ASSERT_EQ(bus.read32(task_addr), 0u);

    // Set event
    bus.write32(ev_addr, 1);
    ppi.tick(1); // Rising edge detected
    ASSERT_EQ(bus.read32(task_addr), 1u);

    // No re-trigger on same event (no edge)
    bus.write32(task_addr, 0);
    ppi.tick(2);
    ASSERT_EQ(bus.read32(task_addr), 0u);
}

void test_tick_fork() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    PPI ppi;
    ppi.set_bus(&bus);

    ppi.write(0x510, Width::Word, 0x20000100); // EEP
    ppi.write(0x514, Width::Word, 0x20000200); // TEP
    ppi.write(0x910, Width::Word, 0x20000300); // FORK_TEP[0]
    ppi.write(0x500, Width::Word, 1);

    bus.write32(0x20000100, 1); // Set event
    ppi.tick(1);

    ASSERT_EQ(bus.read32(0x20000200), 1u); // TEP triggered
    ASSERT_EQ(bus.read32(0x20000300), 1u); // Fork TEP also triggered
}

void test_disabled_channel() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    PPI ppi;
    ppi.set_bus(&bus);

    ppi.write(0x510, Width::Word, 0x20000100);
    ppi.write(0x514, Width::Word, 0x20000200);
    // Don't enable channel
    bus.write32(0x20000100, 1);
    ppi.tick(1);
    ASSERT_EQ(bus.read32(0x20000200), 0u); // Not triggered
}

int main() {
    RUN(test_chen);
    RUN(test_channel_config);
    RUN(test_channel_groups);
    RUN(test_fork_tep);
    RUN(test_tick_event_routing);
    RUN(test_tick_fork);
    RUN(test_disabled_channel);
    TEST_REPORT();
}
