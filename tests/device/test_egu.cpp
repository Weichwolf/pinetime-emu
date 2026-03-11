#include "../test.hpp"
#include "device/egu.hpp"

using namespace pt;

void test_trigger() {
    EGU egu;
    egu.write(0x000, Width::Word, 1); // TRIGGER[0]
    ASSERT_EQ(egu.read(0x100, Width::Word), 1u); // EVENTS_TRIGGERED[0]
    ASSERT_EQ(egu.read(0x104, Width::Word), 0u); // [1] not triggered
}

void test_trigger_multiple() {
    EGU egu;
    egu.write(0x000, Width::Word, 1); // TRIGGER[0]
    egu.write(0x01C, Width::Word, 1); // TRIGGER[7]
    ASSERT_EQ(egu.read(0x100, Width::Word), 1u);
    ASSERT_EQ(egu.read(0x11C, Width::Word), 1u);
}

void test_event_clear() {
    EGU egu;
    egu.write(0x000, Width::Word, 1);
    ASSERT_EQ(egu.read(0x100, Width::Word), 1u);
    egu.write(0x100, Width::Word, 0); // Clear
    ASSERT_EQ(egu.read(0x100, Width::Word), 0u);
}

void test_irq() {
    EGU egu;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    egu.set_nvic(&nvic, 20);

    egu.write(0x304, Width::Word, 1u << 3); // Enable channel 3 interrupt
    egu.write(0x00C, Width::Word, 1);       // TRIGGER[3]
    ASSERT(nvic.is_pending(20));
}

void test_irq_not_fired_without_enable() {
    EGU egu;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    egu.set_nvic(&nvic, 20);

    egu.write(0x000, Width::Word, 1); // TRIGGER[0] but no INTENSET
    ASSERT(!nvic.is_pending(20));
}

void test_inten() {
    EGU egu;
    egu.write(0x304, Width::Word, 0xFFFF);
    ASSERT_EQ(egu.read(0x300, Width::Word), 0xFFFFu);
    egu.write(0x308, Width::Word, 0x00FF);
    ASSERT_EQ(egu.read(0x300, Width::Word), 0xFF00u);
}

int main() {
    RUN(test_trigger);
    RUN(test_trigger_multiple);
    RUN(test_event_clear);
    RUN(test_irq);
    RUN(test_irq_not_fired_without_enable);
    RUN(test_inten);
    TEST_REPORT();
}
