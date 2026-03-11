#include "../test.hpp"
#include "device/nvic.hpp"

using namespace pt;

void test_initial_state() {
    NVIC nvic;
    ASSERT(!nvic.is_enabled(0));
    ASSERT(!nvic.is_pending(0));
    ASSERT_EQ(nvic.get_vtor(), 0u);
}

void test_iser_icer() {
    NVIC nvic;
    // ISER0: write to enable IRQ 5
    nvic.write(0x100, Width::Word, 1u << 5);
    ASSERT(nvic.is_enabled(5));
    ASSERT(!nvic.is_enabled(4));
    // ICER0: write to disable IRQ 5
    nvic.write(0x180, Width::Word, 1u << 5);
    ASSERT(!nvic.is_enabled(5));
}

void test_ispr_icpr() {
    NVIC nvic;
    bool irq_pending = false;
    bool event_reg = false;
    nvic.set_irq_pending_flag(&irq_pending);
    nvic.set_event_reg(&event_reg);

    nvic.write(0x200, Width::Word, 1u << 3); // ISPR: pend IRQ 3
    ASSERT(nvic.is_pending(3));
    ASSERT(irq_pending);
    ASSERT(event_reg);

    nvic.write(0x280, Width::Word, 1u << 3); // ICPR: clear
    ASSERT(!nvic.is_pending(3));
}

void test_set_pending_api() {
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    nvic.set_pending(10);
    ASSERT(nvic.is_pending(10));
    ASSERT(irq_pending);
    nvic.clear_pending(10);
    ASSERT(!nvic.is_pending(10));
}

void test_priority() {
    NVIC nvic;
    // IPR0: set priority of IRQ 0..3
    nvic.write(0x400, Width::Word, 0x40302010);
    ASSERT_EQ(nvic.priority(0), 0x10);
    ASSERT_EQ(nvic.priority(1), 0x20);
    ASSERT_EQ(nvic.priority(2), 0x30);
    ASSERT_EQ(nvic.priority(3), 0x40);
}

void test_get_pending_irq() {
    // NVIC register layout: IPR at NVIC addr 0x400+
    // write_nvic receives off = addr - 0x100
    // IPR starts at off 0x300 -> addr 0x400
    // Each word covers 4 IRQs: addr 0x400 = IRQ 0-3, 0x404 = IRQ 4-7, 0x408 = IRQ 8-11
    // ipr_[idx] = byte within the word (little endian)
    NVIC nvic;
    nvic.set_pending(5);
    nvic.set_pending(10);
    // Enable both
    nvic.write(0x100, Width::Word, (1u << 5) | (1u << 10));
    // IRQ 5: in word at 0x404, byte 1 (IRQ 4=byte0, 5=byte1, 6=byte2, 7=byte3)
    nvic.write(0x404, Width::Word, 0x00004000); // ipr_[5] = 0x40
    // IRQ 10: in word at 0x408, byte 2 (IRQ 8=byte0, 9=byte1, 10=byte2, 11=byte3)
    nvic.write(0x408, Width::Word, 0x00200000); // ipr_[10] = 0x20
    int best = nvic.get_pending_irq();
    ASSERT_EQ(best, 10); // IRQ 10 has higher priority (0x20 < 0x40)
}

void test_vtor() {
    NVIC nvic;
    // VTOR at SCB offset 0x08, NVIC addr 0xD08
    nvic.write(0xD08, Width::Word, 0x00010000);
    ASSERT_EQ(nvic.get_vtor(), 0x00010000u);
}

void test_systick() {
    NVIC nvic;
    // SysTick CSR at 0x010
    nvic.write(0x010, Width::Word, 0x7); // ENABLE|TICKINT|CLKSOURCE
    // RVR
    nvic.write(0x014, Width::Word, 1000);
    // CVR (write clears)
    nvic.write(0x018, Width::Word, 0);
    ASSERT_EQ(nvic.read(0x014, Width::Word), 1000u);
    // Tick until countdown
    for (uint64_t cy = 0; cy <= 1000; cy++)
        nvic.tick(cy);
    ASSERT(nvic.is_sys_pending(15)); // SysTick exception
}

void test_cpuid() {
    NVIC nvic;
    ASSERT_EQ(nvic.read(0xD00, Width::Word), 0x410FC241u); // Cortex-M4
}

void test_scb_shpr() {
    NVIC nvic;
    // SHPR3: PendSV (exc 14) and SysTick (exc 15)
    // SHPR3 at SCB offset 0x20, NVIC addr 0xD20
    nvic.write(0xD20, Width::Word, 0xE0C00000);
    ASSERT_EQ(nvic.system_priority(14), 0xC0); // PendSV = shpr_[10]
    ASSERT_EQ(nvic.system_priority(15), 0xE0); // SysTick = shpr_[11]
}

void test_sysresetreq() {
    NVIC nvic;
    // AIRCR: VECTKEY=0x05FA, SYSRESETREQ=bit2
    nvic.write(0xD0C, Width::Word, 0x05FA0004);
    ASSERT(nvic.check_sysreset());
    // After reset, VTOR should be 0
    ASSERT_EQ(nvic.get_vtor(), 0u);
}

void test_pendsv() {
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    // ICSR: PENDSVSET = bit 28
    nvic.write(0xD04, Width::Word, 1u << 28);
    ASSERT(nvic.is_sys_pending(14));
    // PENDSVCLR = bit 27
    nvic.write(0xD04, Width::Word, 1u << 27);
    ASSERT(!nvic.is_sys_pending(14));
}

int main() {
    RUN(test_initial_state);
    RUN(test_iser_icer);
    RUN(test_ispr_icpr);
    RUN(test_set_pending_api);
    RUN(test_priority);
    RUN(test_get_pending_irq);
    RUN(test_vtor);
    RUN(test_systick);
    RUN(test_cpuid);
    RUN(test_scb_shpr);
    RUN(test_sysresetreq);
    RUN(test_pendsv);
    TEST_REPORT();
}
