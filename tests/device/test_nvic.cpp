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

void test_icsr_pendst() {
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    // PENDSTSET (bit 26)
    nvic.write(0xD04, Width::Word, 1u << 26);
    ASSERT(nvic.is_sys_pending(15)); // SysTick
    // PENDSTCLR (bit 25)
    nvic.write(0xD04, Width::Word, 1u << 25);
    ASSERT(!nvic.is_sys_pending(15));
}

void test_cpacr() {
    NVIC nvic;
    // CPACR at 0xD88
    nvic.write(0xD88, Width::Word, 0x00F00000); // CP10+CP11 full access
    ASSERT_EQ(nvic.read(0xD88, Width::Word), 0x00F00000u);
}

void test_fpccr() {
    NVIC nvic;
    // FPCCR default: ASPEN=1, LSPEN=1
    ASSERT_EQ(nvic.read(0xF34, Width::Word), 0xC0000000u);
    nvic.write(0xF34, Width::Word, 0x80000000);
    ASSERT_EQ(nvic.read(0xF34, Width::Word), 0x80000000u);
}

void test_fpcar() {
    NVIC nvic;
    nvic.write(0xF38, Width::Word, 0x20001000);
    ASSERT_EQ(nvic.read(0xF38, Width::Word), 0x20001000u);
}

void test_fpdscr() {
    NVIC nvic;
    nvic.write(0xF3C, Width::Word, 0x07C00000);
    ASSERT_EQ(nvic.read(0xF3C, Width::Word), 0x07C00000u);
}

void test_systick_countflag() {
    NVIC nvic;
    nvic.write(0x010, Width::Word, 0x5); // ENABLE|CLKSOURCE (no TICKINT)
    nvic.write(0x014, Width::Word, 10);   // RVR=10
    nvic.write(0x018, Width::Word, 0);    // Clear CVR
    for (uint64_t cy = 0; cy <= 12; cy++)
        nvic.tick(cy);
    uint32_t csr = nvic.read(0x010, Width::Word);
    ASSERT(csr & (1u << 16)); // COUNTFLAG set
}

void test_systick_calib() {
    NVIC nvic;
    ASSERT_EQ(nvic.read(0x01C, Width::Word), 0x00FFFFFFu);
}

void test_active_bits() {
    NVIC nvic;
    nvic.set_active(5);
    // IABR0 at offset 0x300 from SCS -> NVIC at 0x200+0x100=0x300
    // But read_nvic offset = addr - 0x100, IABR starts at 0x200
    uint32_t iabr = nvic.read(0x300, Width::Word); // IABR0
    ASSERT(iabr & (1u << 5));
    nvic.clear_active(5);
    iabr = nvic.read(0x300, Width::Word);
    ASSERT(!(iabr & (1u << 5)));
}

void test_scr() {
    NVIC nvic;
    nvic.write(0xD10, Width::Word, 0x16);
    ASSERT_EQ(nvic.read(0xD10, Width::Word), 0x16u & 0x1Eu);
}

void test_ccr() {
    NVIC nvic;
    ASSERT_EQ(nvic.read(0xD14, Width::Word), 0x200u); // STKALIGN default
}

void test_stir() {
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    // STIR at 0xF00: Software Trigger Interrupt Register
    nvic.write(0xF00, Width::Word, 7); // Pend IRQ 7
    ASSERT(nvic.is_pending(7));
}

void test_shcsr() {
    NVIC nvic;
    nvic.write(0xD24, Width::Word, 0x00070000); // Enable MemManage, BusFault, UsageFault
    ASSERT_EQ(nvic.read(0xD24, Width::Word), 0x00070000u);
}

void test_aircr_prigroup() {
    NVIC nvic;
    // VECTKEY=0x05FA, PRIGROUP=5
    nvic.write(0xD0C, Width::Word, 0x05FA0500);
    uint32_t aircr = nvic.read(0xD0C, Width::Word);
    ASSERT_EQ(aircr & 0x700, 0x500u);
}

void test_get_pending_priority() {
    NVIC nvic;
    nvic.write(0x100, Width::Word, (1u << 3) | (1u << 7));
    nvic.set_pending(3);
    nvic.set_pending(7);
    nvic.write(0x400, Width::Word, 0x20000000); // IRQ3=0x20
    nvic.write(0x404, Width::Word, 0x00000040); // IRQ4=0x40 (irrelevant)
    nvic.write(0x404, Width::Word, 0x00000000); // clear
    // IRQ 7 in IPR word at 0x404: byte 3 = IRQ7
    nvic.write(0x404, Width::Word, 0x60000000); // IRQ7=0x60
    ASSERT_EQ(nvic.get_pending_irq(), 3); // IRQ3 (pri 0x20) < IRQ7 (pri 0x60)
}

void test_sys_exception_priority() {
    NVIC nvic;
    ASSERT_EQ(nvic.system_priority(2), 0xFEu); // NMI = -2
    ASSERT_EQ(nvic.system_priority(3), 0xFFu); // HardFault = -1
    // Default configurable exception priority = 0
    ASSERT_EQ(nvic.system_priority(4), 0u);
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
    RUN(test_icsr_pendst);
    RUN(test_cpacr);
    RUN(test_fpccr);
    RUN(test_fpcar);
    RUN(test_fpdscr);
    RUN(test_systick_countflag);
    RUN(test_systick_calib);
    RUN(test_active_bits);
    RUN(test_scr);
    RUN(test_ccr);
    RUN(test_stir);
    RUN(test_shcsr);
    RUN(test_aircr_prigroup);
    RUN(test_get_pending_priority);
    RUN(test_sys_exception_priority);
    TEST_REPORT();
}
