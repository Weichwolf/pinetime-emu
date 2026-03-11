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

// Helper: send command + args
static void lcd_cmd(ST7789& lcd, uint8_t cmd) {
    lcd.set_dc(false); lcd.spi_transfer(&cmd, 1);
}
static void lcd_data(ST7789& lcd, const uint8_t* d, uint32_t len) {
    lcd.set_dc(true); lcd.spi_transfer(d, len);
}
static void lcd_set_window(ST7789& lcd, uint16_t cs, uint16_t ce, uint16_t rs, uint16_t re) {
    lcd_cmd(lcd, 0x2A);
    uint8_t ca[] = {(uint8_t)(cs>>8),(uint8_t)cs,(uint8_t)(ce>>8),(uint8_t)ce};
    lcd_data(lcd, ca, 4);
    lcd_cmd(lcd, 0x2B);
    uint8_t ra[] = {(uint8_t)(rs>>8),(uint8_t)rs,(uint8_t)(re>>8),(uint8_t)re};
    lcd_data(lcd, ra, 4);
}
static void lcd_write_pixel(ST7789& lcd, uint16_t color) {
    uint8_t p[] = {(uint8_t)(color>>8), (uint8_t)color};
    lcd_data(lcd, p, 2);
}

void test_multi_pixel_ramwr() {
    ST7789 lcd;
    lcd_cmd(lcd, 0x11); // SLPOUT
    lcd_cmd(lcd, 0x29); // DISPON
    lcd_set_window(lcd, 0, 2, 0, 0); // 3 columns, 1 row
    lcd_cmd(lcd, 0x2C); // RAMWR
    lcd_write_pixel(lcd, 0xF800); // Red
    lcd_write_pixel(lcd, 0x07E0); // Green
    lcd_write_pixel(lcd, 0x001F); // Blue
    ASSERT_EQ(lcd.pixels_written_, 3u);
    const uint16_t* fb = lcd.framebuffer();
    ASSERT_EQ(fb[0], 0xF800); // Row 0, Col 0
    ASSERT_EQ(fb[1], 0x07E0); // Row 0, Col 1
    ASSERT_EQ(fb[2], 0x001F); // Row 0, Col 2
}

void test_window_wrap() {
    ST7789 lcd;
    lcd_set_window(lcd, 0, 1, 0, 1); // 2x2 window
    lcd_cmd(lcd, 0x2C);
    lcd_write_pixel(lcd, 0x0001); // (0,0)
    lcd_write_pixel(lcd, 0x0002); // (0,1) -> col wraps
    lcd_write_pixel(lcd, 0x0003); // (1,0) -> next row
    lcd_write_pixel(lcd, 0x0004); // (1,1)
    lcd_write_pixel(lcd, 0x0005); // (0,0) -> row wraps back

    const uint16_t* fb = lcd.framebuffer();
    // Row 0 in controller = out row 0
    ASSERT_EQ(fb[0*240+0], 0x0005); // Wrapped back: overwritten (0,0)
    ASSERT_EQ(fb[0*240+1], 0x0002);
    ASSERT_EQ(fb[1*240+0], 0x0003);
    ASSERT_EQ(fb[1*240+1], 0x0004);
}

void test_scroll_composition_with_pixels() {
    ST7789 lcd;
    // VSCRDEF: TFA=80, VSA=160, BFA=80
    lcd_cmd(lcd, 0x33);
    uint8_t vsc[] = {0, 80, 0, 160, 0, 80};
    lcd_data(lcd, vsc, 6);

    // Write pixel at controller row 100, col 5
    lcd_set_window(lcd, 5, 5, 100, 100);
    lcd_cmd(lcd, 0x2C);
    lcd_write_pixel(lcd, 0xBBBB);

    // Write pixel at controller row 120, col 0
    lcd_set_window(lcd, 0, 0, 120, 120);
    lcd_cmd(lcd, 0x2C);
    lcd_write_pixel(lcd, 0xCCCC);

    // Set VSCSAD=80 -> "no scroll": row 80 at top of VSA
    lcd_cmd(lcd, 0x37);
    uint8_t sa[] = {0, 80};
    lcd_data(lcd, sa, 2);

    const uint16_t* fb = lcd.framebuffer();
    // visible_vsa = 240-80-80=80 rows. scroll_offset = 80-80 = 0.
    // out_row 80 -> src_row = 80+0 = 80 (controller row 80)
    // out_row 100 -> src_row = 100 (controller row 100) -> 0xBBBB at col 5
    ASSERT_EQ(fb[100*240+5], 0xBBBB);
    // out_row 120 -> src_row = 120 -> 0xCCCC at col 0
    ASSERT_EQ(fb[120*240+0], 0xCCCC);

    // Now scroll: VSCSAD=100 -> row 100 at top of VSA
    lcd_cmd(lcd, 0x37);
    uint8_t sa2[] = {0, 100};
    lcd_data(lcd, sa2, 2);

    fb = lcd.framebuffer();
    // scroll_offset = 100-80 = 20
    // out_row 80 -> src_row = 80 + (20+0)%160 = 100 -> 0xBBBB at col 5
    ASSERT_EQ(fb[80*240+5], 0xBBBB);
    // out_row 100 -> src_row = 80 + (20+20)%160 = 120 -> 0xCCCC at col 0
    ASSERT_EQ(fb[100*240+0], 0xCCCC);
}

void test_inversion_state() {
    ST7789 lcd;
    lcd_cmd(lcd, 0x21); // INVON
    // Just verify it doesn't crash; inversion is display-side
    lcd_cmd(lcd, 0x20); // INVOFF
}

void test_colmod() {
    ST7789 lcd;
    lcd_cmd(lcd, 0x3A);
    uint8_t mode = 0x55; // RGB565
    lcd_data(lcd, &mode, 1);
    // No crash, config accepted
}

void test_pixel_clipping() {
    ST7789 lcd;
    // Window beyond framebuffer bounds
    lcd_set_window(lcd, 0, 0, 320, 320); // row 320 = out of bounds
    lcd_cmd(lcd, 0x2C);
    lcd_write_pixel(lcd, 0x1234);
    ASSERT_EQ(lcd.pixels_clipped_, 1u);
    ASSERT_EQ(lcd.pixels_written_, 0u);
}

void test_ramwr_count() {
    ST7789 lcd;
    lcd_cmd(lcd, 0x2C);
    ASSERT_EQ(lcd.ramwr_count_, 1u);
    lcd_cmd(lcd, 0x2C);
    ASSERT_EQ(lcd.ramwr_count_, 2u);
}

void test_tfa_pixels_not_scrolled() {
    ST7789 lcd;
    // TFA=40, VSA=240, BFA=40
    lcd_cmd(lcd, 0x33);
    uint8_t vsc[] = {0, 40, 0, 240, 0, 40};
    lcd_data(lcd, vsc, 6);

    // Write to TFA row 10
    lcd_set_window(lcd, 0, 0, 10, 10);
    lcd_cmd(lcd, 0x2C);
    lcd_write_pixel(lcd, 0x5555);

    // Scroll shouldn't affect TFA
    lcd_cmd(lcd, 0x37);
    uint8_t sa[] = {0, 100};
    lcd_data(lcd, sa, 2);

    const uint16_t* fb = lcd.framebuffer();
    ASSERT_EQ(fb[10*240+0], 0x5555); // TFA row unaffected by scroll
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
    RUN(test_multi_pixel_ramwr);
    RUN(test_window_wrap);
    RUN(test_scroll_composition_with_pixels);
    RUN(test_inversion_state);
    RUN(test_colmod);
    RUN(test_pixel_clipping);
    RUN(test_ramwr_count);
    RUN(test_tfa_pixels_not_scrolled);
    TEST_REPORT();
}
