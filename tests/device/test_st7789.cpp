#include "../test.hpp"
#include "device/st7789.hpp"

using namespace pt;

void test_initial_state() {
    ST7789 lcd;
    ASSERT(!lcd.is_on()); // sleep=true, display_on=false
}

void test_sleep_wake() {
    ST7789 lcd;
    uint8_t cmd;
    cmd = 0x11; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1); // SLPOUT
    cmd = 0x29; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1); // DISPON
    ASSERT(lcd.is_on());
    cmd = 0x10; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1); // SLPIN
    ASSERT(!lcd.is_on());
}

void test_display_off() {
    ST7789 lcd;
    uint8_t cmd;
    cmd = 0x11; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    cmd = 0x29; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    ASSERT(lcd.is_on());
    cmd = 0x28; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1); // DISPOFF
    ASSERT(!lcd.is_on());
}

void test_caset_raset() {
    ST7789 lcd;
    uint8_t cmd;
    // CASET: col 10..50
    cmd = 0x2A; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t args[] = {0, 10, 0, 50};
    lcd.set_dc(true); lcd.spi_transfer(args, 4);
    // RASET: row 20..60
    cmd = 0x2B; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t args2[] = {0, 20, 0, 60};
    lcd.set_dc(true); lcd.spi_transfer(args2, 4);
}

void test_pixel_write() {
    ST7789 lcd;
    uint8_t cmd;
    // Setup window at (0,0)-(0,0) single pixel
    cmd = 0x2A; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t caset[] = {0, 0, 0, 0};
    lcd.set_dc(true); lcd.spi_transfer(caset, 4);
    cmd = 0x2B; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t raset[] = {0, 80, 0, 80}; // Row 80 (visible on PineTime)
    lcd.set_dc(true); lcd.spi_transfer(raset, 4);

    // RAMWR
    cmd = 0x2C; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    // Write one pixel (RGB565 = 0xF800 = red)
    uint8_t pixel[] = {0xF8, 0x00};
    lcd.set_dc(true); lcd.spi_transfer(pixel, 2);

    ASSERT_EQ(lcd.pixels_written_, 1u);
}

void test_swreset() {
    ST7789 lcd;
    uint8_t cmd;
    cmd = 0x11; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    cmd = 0x29; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    ASSERT(lcd.is_on());
    cmd = 0x01; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1); // SWRESET
    ASSERT(!lcd.is_on());
}

void test_madctl() {
    ST7789 lcd;
    uint8_t cmd = 0x36; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t arg = 0x70; // MX+MV+MY
    lcd.set_dc(true); lcd.spi_transfer(&arg, 1);
}

void test_vscrdef() {
    ST7789 lcd;
    uint8_t cmd = 0x33; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t args[] = {0, 80, 0, 160, 0, 80}; // TFA=80, VSA=160, BFA=80
    lcd.set_dc(true); lcd.spi_transfer(args, 6);
}

void test_vscsad() {
    ST7789 lcd;
    uint8_t cmd = 0x37; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t args[] = {0, 100}; // Scroll to row 100
    lcd.set_dc(true); lcd.spi_transfer(args, 2);
}

void test_framebuffer_compose() {
    ST7789 lcd;
    // Default: no scroll, TFA=0, VSA=320, BFA=0
    // Write pixel at row 80 col 0 (first visible PineTime row)
    uint8_t cmd;
    cmd = 0x2A; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t caset[] = {0, 0, 0, 239};
    lcd.set_dc(true); lcd.spi_transfer(caset, 4);
    cmd = 0x2B; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t raset[] = {0, 0, 0, 0}; // Row 0
    lcd.set_dc(true); lcd.spi_transfer(raset, 4);

    cmd = 0x2C; lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    uint8_t pixel[] = {0xF8, 0x00}; // Red
    lcd.set_dc(true); lcd.spi_transfer(pixel, 2);

    const uint16_t* fb = lcd.framebuffer();
    ASSERT_EQ(fb[0], 0xF800); // Row 0, Col 0 = red
}

void test_cmd_data_counts() {
    ST7789 lcd;
    ASSERT_EQ(lcd.cmd_count(), 0u);
    ASSERT_EQ(lcd.data_bytes(), 0u);
    uint8_t cmd = 0x11;
    lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
    ASSERT_EQ(lcd.cmd_count(), 1u);
    uint8_t data[] = {0x01, 0x02, 0x03};
    lcd.set_dc(true); lcd.spi_transfer(data, 3);
    ASSERT_EQ(lcd.data_bytes(), 3u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_sleep_wake);
    RUN(test_display_off);
    RUN(test_caset_raset);
    RUN(test_pixel_write);
    RUN(test_swreset);
    RUN(test_madctl);
    RUN(test_vscrdef);
    RUN(test_vscsad);
    RUN(test_framebuffer_compose);
    RUN(test_cmd_data_counts);
    TEST_REPORT();
}
