#include "../test.hpp"
#include "device/gpio.hpp"

using namespace pt;

void test_out_write_read() {
    GPIO gpio;
    gpio.write(0x504, Width::Word, 0x12345678);
    ASSERT_EQ(gpio.read(0x504, Width::Word), 0x12345678u);
}

void test_outset() {
    GPIO gpio;
    gpio.write(0x504, Width::Word, 0x0F);
    gpio.write(0x508, Width::Word, 0xF0); // OUTSET
    ASSERT_EQ(gpio.read(0x504, Width::Word), 0xFFu);
    ASSERT_EQ(gpio.read(0x508, Width::Word), 0xFFu); // OUTSET reads as OUT
}

void test_outclr() {
    GPIO gpio;
    gpio.write(0x504, Width::Word, 0xFF);
    gpio.write(0x50C, Width::Word, 0x0F); // OUTCLR
    ASSERT_EQ(gpio.read(0x504, Width::Word), 0xF0u);
}

void test_dir() {
    GPIO gpio;
    gpio.write(0x514, Width::Word, 0xFF);
    ASSERT_EQ(gpio.read(0x514, Width::Word), 0xFFu);
    gpio.write(0x518, Width::Word, 0x100); // DIRSET
    ASSERT_EQ(gpio.read(0x514, Width::Word), 0x1FFu);
    gpio.write(0x51C, Width::Word, 0x0F); // DIRCLR
    ASSERT_EQ(gpio.read(0x514, Width::Word), 0x1F0u);
}

void test_pin_cnf() {
    GPIO gpio;
    gpio.write(0x700, Width::Word, 0x00030003); // PIN_CNF[0]
    ASSERT_EQ(gpio.pin_cnf(0), 0x00030003u);
}

void test_in_reflects_inputs() {
    GPIO gpio;
    gpio.set_pin(5, true);
    uint32_t in = gpio.read(0x510, Width::Word);
    ASSERT(in & (1u << 5));
}

void test_in_shows_output_for_output_pins() {
    GPIO gpio;
    gpio.write(0x514, Width::Word, 1u << 3); // pin 3 = output
    gpio.write(0x504, Width::Word, 1u << 3); // OUT pin 3 = high
    uint32_t in = gpio.read(0x510, Width::Word);
    ASSERT(in & (1u << 3));
}

void test_latch() {
    GPIO gpio;
    // Set SENSE=High on pin 2
    gpio.write(0x700 + 2*4, Width::Word, 0x00020000);
    gpio.set_pin(2, true);
    ASSERT(gpio.read(0x520, Width::Word) & (1u << 2));
    // Clear latch
    gpio.write(0x520, Width::Word, 1u << 2);
    ASSERT_EQ(gpio.read(0x520, Width::Word) & (1u << 2), 0u);
}

void test_sense_auto_toggle() {
    GPIO gpio;
    // Set SENSE=High (2) on pin 0
    gpio.write(0x700, Width::Word, 0x00020000);
    gpio.set_pin(0, true); // pin is high
    // Read PIN_CNF[0]: should return original value but toggle internally
    uint32_t val = gpio.read(0x700, Width::Word);
    ASSERT_EQ((val >> 16) & 3, 2u); // Returns original
    // After read, internal should be toggled to Low (3)
    ASSERT_EQ((gpio.pin_cnf(0) >> 16) & 3, 3u);
}

void test_gpiote_event_mode() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);

    // Config channel 0: Event mode, pin 5, LoToHi
    gpiote.write(0x510, Width::Word, (5 << 8) | (1 << 16) | 1);
    // Enable IN[0] interrupt
    gpiote.write(0x304, Width::Word, 1u << 0);

    gpio.set_pin(5, true); // Rising edge
    ASSERT_EQ(gpiote.read(0x100, Width::Word), 1u); // EVENTS_IN[0]
    ASSERT(nvic.is_pending(6));
}

void test_gpiote_port_event() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);

    // Enable PORT interrupt (bit 31)
    gpiote.write(0x304, Width::Word, 1u << 31);

    gpio.set_pin(10, true);
    ASSERT_EQ(gpiote.get_ev_port(), 1u);
    ASSERT(nvic.is_pending(6));
}

void test_gpiote_inten() {
    GPIOTE gpiote;
    gpiote.write(0x304, Width::Word, 0xFF);
    ASSERT_EQ(gpiote.read(0x300, Width::Word), 0xFFu);
    gpiote.write(0x308, Width::Word, 0x0F);
    ASSERT_EQ(gpiote.read(0x300, Width::Word), 0xF0u);
}

int main() {
    RUN(test_out_write_read);
    RUN(test_outset);
    RUN(test_outclr);
    RUN(test_dir);
    RUN(test_pin_cnf);
    RUN(test_in_reflects_inputs);
    RUN(test_in_shows_output_for_output_pins);
    RUN(test_latch);
    RUN(test_sense_auto_toggle);
    RUN(test_gpiote_event_mode);
    RUN(test_gpiote_port_event);
    RUN(test_gpiote_inten);
    TEST_REPORT();
}
