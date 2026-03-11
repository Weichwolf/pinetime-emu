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

void test_sense_detect_edge() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);

    // Enable PORT interrupt
    gpiote.write(0x304, Width::Word, 1u << 31);

    // SENSE=High on pin 0: PORT fires when pin goes high
    gpio.write(0x700, Width::Word, 0x00020000);
    ASSERT_EQ(gpiote.get_ev_port(), 0u); // No event yet

    gpio.set_pin(0, true); // DETECT rises → PORT
    ASSERT_EQ(gpiote.get_ev_port(), 1u);

    // Clear PORT, pin stays high, SENSE stays High → DETECT still active
    gpiote.write(0x17C, Width::Word, 0);

    // Pin goes low → DETECT falls, no PORT (falling edge of DETECT)
    gpio.set_pin(0, false);
    ASSERT_EQ(gpiote.get_ev_port(), 0u); // No new PORT

    // Firmware toggles SENSE to Low (to catch next low)
    gpio.write(0x700, Width::Word, 0x00030000);
    // Pin is already low, SENSE=Low → DETECT rises → PORT!
    ASSERT_EQ(gpiote.get_ev_port(), 1u);

    // Read PIN_CNF — returns original, but auto-toggles SENSE internally
    // (pin is low, SENSE=Low matches → toggle to High for next edge)
    uint32_t val = gpio.read(0x700, Width::Word);
    ASSERT_EQ((val >> 16) & 3, 3u); // Returns original SENSE=Low
    ASSERT_EQ((gpio.pin_cnf(0) >> 16) & 3, 2u); // Internally toggled to High
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

    // Set SENSE=High on pin 10 so DETECT fires when pin goes high
    gpio.write(0x700 + 10*4, Width::Word, 0x00020000);

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

void test_sense_toggle_cycle() {
    // InfiniTime-style: firmware reads PIN_CNF (auto-toggle), never writes SENSE
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);
    gpiote.write(0x304, Width::Word, 1u << 31); // Enable PORT

    // Pin 0: SENSE=Low (3) — detect when pin goes low (like CST816S)
    gpio.write(0x700, Width::Word, 0x00030000);

    // Touch down: pin goes low → DETECT rises → PORT
    gpio.set_pin(0, false);
    ASSERT(gpio.read(0x520, Width::Word) & 1);
    ASSERT_EQ(gpiote.get_ev_port(), 1u);

    // ISR: clear PORT, read PIN_CNF (auto-toggles SENSE Low→High)
    gpiote.write(0x17C, Width::Word, 0);
    gpio.read(0x700, Width::Word);
    ASSERT_EQ((gpio.pin_cnf(0) >> 16) & 3, 2u); // Toggled to High

    // Touch release: pin goes high → SENSE=High matches → DETECT rises → PORT
    gpio.set_pin(0, true);
    ASSERT_EQ(gpiote.get_ev_port(), 1u);

    // ISR: clear PORT, read PIN_CNF (auto-toggles SENSE High→Low)
    gpiote.write(0x17C, Width::Word, 0);
    gpio.read(0x700, Width::Word);
    ASSERT_EQ((gpio.pin_cnf(0) >> 16) & 3, 3u); // Back to Low

    // Second touch: pin goes low again → works
    gpio.set_pin(0, false);
    ASSERT_EQ(gpiote.get_ev_port(), 1u);
}

void test_gpiote_hitolo() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);

    // Config channel 1: Event mode, pin 3, HiToLo (polarity=2)
    gpiote.write(0x514, Width::Word, (3 << 8) | (2 << 16) | 1);
    gpiote.write(0x304, Width::Word, 1u << 1); // Enable IN[1]

    gpio.set_pin(3, true);
    ASSERT_EQ(gpiote.read(0x104, Width::Word), 0u); // No event on rising

    gpio.set_pin(3, false); // Falling edge
    ASSERT_EQ(gpiote.read(0x104, Width::Word), 1u); // IN[1] event
    ASSERT(nvic.is_pending(6));
}

void test_gpiote_toggle_polarity() {
    GPIO gpio;
    GPIOTE gpiote;
    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);

    // Channel 0: pin 7, Toggle (polarity=3)
    gpiote.write(0x510, Width::Word, (7 << 8) | (3 << 16) | 1);

    gpio.set_pin(7, true); // Rising
    ASSERT_EQ(gpiote.read(0x100, Width::Word), 1u);
    gpiote.write(0x100, Width::Word, 0); // Clear

    gpio.set_pin(7, false); // Falling
    ASSERT_EQ(gpiote.read(0x100, Width::Word), 1u); // Also triggers
}

void test_gpiote_multi_channel() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);

    // Channel 0: pin 5, LoToHi
    gpiote.write(0x510, Width::Word, (5 << 8) | (1 << 16) | 1);
    // Channel 2: pin 10, LoToHi
    gpiote.write(0x518, Width::Word, (10 << 8) | (1 << 16) | 1);
    gpiote.write(0x304, Width::Word, (1u << 0) | (1u << 2)); // Enable IN[0] and IN[2]

    gpio.set_pin(5, true);
    ASSERT_EQ(gpiote.read(0x100, Width::Word), 1u); // IN[0]
    ASSERT_EQ(gpiote.read(0x108, Width::Word), 0u); // IN[2] not yet

    gpio.set_pin(10, true);
    ASSERT_EQ(gpiote.read(0x108, Width::Word), 1u); // IN[2]
}

void test_gpio_out_callback() {
    GPIO gpio;
    uint32_t old_out = 0, new_out = 0;
    int calls = 0;
    gpio.set_on_out_change([&](uint32_t o, uint32_t n) {
        old_out = o; new_out = n; calls++;
    });
    gpio.write(0x504, Width::Word, 0xFF);
    ASSERT_EQ(calls, 1);
    ASSERT_EQ(old_out, 0u);
    ASSERT_EQ(new_out, 0xFFu);
}

void test_gpiote_event_clear() {
    GPIO gpio;
    GPIOTE gpiote;
    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);

    gpiote.write(0x510, Width::Word, (5 << 8) | (1 << 16) | 1);
    gpio.set_pin(5, true);
    ASSERT_EQ(gpiote.read(0x100, Width::Word), 1u);
    gpiote.write(0x100, Width::Word, 0);
    ASSERT_EQ(gpiote.read(0x100, Width::Word), 0u);
}

void test_latch_write_1_clear() {
    GPIO gpio;
    gpio.write(0x700 + 4*4, Width::Word, 0x00020000); // Pin 4 SENSE=High
    gpio.set_pin(4, true);
    ASSERT(gpio.read(0x520, Width::Word) & (1u << 4));
    // Write 1 to clear bit 4
    gpio.write(0x520, Width::Word, 1u << 4);
    ASSERT(!(gpio.read(0x520, Width::Word) & (1u << 4)));
    // Other bits unaffected
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
    RUN(test_sense_detect_edge);
    RUN(test_gpiote_event_mode);
    RUN(test_gpiote_port_event);
    RUN(test_gpiote_inten);
    RUN(test_sense_toggle_cycle);
    RUN(test_gpiote_hitolo);
    RUN(test_gpiote_toggle_polarity);
    RUN(test_gpiote_multi_channel);
    RUN(test_gpio_out_callback);
    RUN(test_gpiote_event_clear);
    RUN(test_latch_write_1_clear);
    TEST_REPORT();
}
