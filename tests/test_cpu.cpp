#include "test.hpp"
#include "bus.hpp"
#include "cpu.hpp"
#include "device/nvic.hpp"
#include "device/memory.hpp"

using namespace pt;

void test_reset() {
    RAM ram(64 * 1024);
    Flash flash(512 * 1024);
    // Vector table: SP at 0x00, PC at 0x04
    uint8_t vt[8] = {0x00, 0x00, 0x01, 0x20, // SP = 0x20010000
                      0x09, 0x00, 0x00, 0x00}; // Reset = 0x09 (thumb)
    memcpy(flash.data(), vt, sizeof(vt));
    // Put a BKPT at 0x08
    flash.data()[8] = 0x00; flash.data()[9] = 0xBE; // BKPT #0

    Bus bus;
    bus.set_fast_path(flash.data(), flash.size(), ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bus.map(0xE000E000, 0x1000, &nvic);

    CPU cpu(&bus);
    cpu.set_nvic(&nvic);
    nvic.set_irq_pending_flag(&cpu.irq_pending);
    nvic.set_event_reg(&cpu.event_reg);
    cpu.reset();

    ASSERT_EQ(cpu.r[13], 0x20010000u);
    ASSERT_EQ(cpu.r[15], 0x08u);
    ASSERT(cpu.xpsr & (1u << 24)); // T bit set
}

void test_flags() {
    Bus bus;
    CPU cpu(&bus);

    cpu.set_flags(true, false, true, false);
    ASSERT(cpu.flag_n());
    ASSERT(!cpu.flag_z());
    ASSERT(cpu.flag_c());
    ASSERT(!cpu.flag_v());

    cpu.set_nz(0);
    ASSERT(!cpu.flag_n());
    ASSERT(cpu.flag_z());

    cpu.set_nz(0x80000000);
    ASSERT(cpu.flag_n());
    ASSERT(!cpu.flag_z());
}

void test_it_state() {
    Bus bus;
    CPU cpu(&bus);
    cpu.xpsr = 0;
    ASSERT(!cpu.in_it_block());
}

void test_sp_banking() {
    Bus bus;
    CPU cpu(&bus);
    cpu.xpsr = 0; // thread mode
    cpu.control = 0;
    cpu.msp = 0x20001000;
    cpu.r[13] = 0x20001000;
    ASSERT_EQ(cpu.sp(), 0x20001000u);

    cpu.control = 2; // SPSEL=1
    cpu.psp = 0x20002000;
    cpu.r[13] = 0x20002000;
    ASSERT_EQ(cpu.sp(), 0x20002000u);

    // In handler mode, always MSP
    cpu.xpsr = 11; // exception active
    ASSERT_EQ(cpu.sp(), 0x20001000u);
}

void test_step_bkpt() {
    Flash flash(1024);
    RAM ram(1024);
    // BKPT #0 at address 0
    flash.data()[0] = 0x00; flash.data()[1] = 0xBE;

    Bus bus;
    bus.set_fast_path(flash.data(), flash.size(), ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bus.map(0xE000E000, 0x1000, &nvic);

    CPU cpu(&bus);
    cpu.set_nvic(&nvic);
    cpu.xpsr = (1u << 24); // T bit
    cpu.r[15] = 0;
    cpu.step();
    ASSERT(cpu.halted);
}

int main() {
    RUN(test_reset);
    RUN(test_flags);
    RUN(test_it_state);
    RUN(test_sp_banking);
    RUN(test_step_bkpt);
    TEST_REPORT();
}
