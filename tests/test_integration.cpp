#include "test.hpp"
#include "bus.hpp"
#include "device/memory.hpp"
#include "device/nvic.hpp"
#include "device/spim.hpp"
#include "device/twim.hpp"
#include "device/gpio.hpp"
#include "device/st7789.hpp"
#include "device/i2c_devices.hpp"
#include "device/spi_flash.hpp"
#include "device/timer.hpp"

using namespace pt;

// ============================================================
// SPIM → ST7789 DMA pipeline (InfiniTime display path)
// ============================================================

void test_spim_to_st7789_cmd() {
    RAM ram(4096);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    SPIM spim;
    ST7789 lcd;
    spim.set_bus(&bus);
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len, uint8_t*, uint32_t) {
        lcd.set_dc(false);
        lcd.spi_transfer(tx, tx_len);
    });

    // Send SLPOUT (0x11) via DMA
    bus.write8(0x20000000, 0x11);
    spim.write(0x500, Width::Word, 7); // ENABLE
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 1);
    spim.write(0x534, Width::Word, 0x20000100);
    spim.write(0x538, Width::Word, 0);
    spim.write(0x010, Width::Word, 1); // START

    ASSERT_EQ(lcd.cmd_count(), 1u);
}

void test_spim_to_st7789_pixel_dma() {
    RAM ram(4096);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    SPIM spim;
    ST7789 lcd;
    bool is_data = false;
    spim.set_bus(&bus);
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len, uint8_t*, uint32_t) {
        lcd.set_dc(is_data);
        lcd.spi_transfer(tx, tx_len);
    });

    auto send_cmd = [&](uint8_t cmd) {
        bus.write8(0x20000000, cmd);
        is_data = false;
        spim.write(0x544, Width::Word, 0x20000000);
        spim.write(0x548, Width::Word, 1);
        spim.write(0x538, Width::Word, 0);
        spim.write(0x010, Width::Word, 1);
    };
    auto send_data = [&](const uint8_t* d, uint32_t len) {
        for (uint32_t i = 0; i < len; i++)
            bus.write8(0x20000800 + i, d[i]);
        is_data = true;
        spim.write(0x544, Width::Word, 0x20000800);
        spim.write(0x548, Width::Word, len);
        spim.write(0x538, Width::Word, 0);
        spim.write(0x010, Width::Word, 1);
    };

    spim.write(0x500, Width::Word, 7);

    // SLPOUT + DISPON
    send_cmd(0x11);
    send_cmd(0x29);

    // CASET 0..2
    send_cmd(0x2A);
    uint8_t caset[] = {0, 0, 0, 2};
    send_data(caset, 4);

    // RASET 0..0
    send_cmd(0x2B);
    uint8_t raset[] = {0, 0, 0, 0};
    send_data(raset, 4);

    // RAMWR + 3 pixels via DMA
    send_cmd(0x2C);
    uint8_t pixels[] = {0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F}; // Red, Green, Blue
    send_data(pixels, 6);

    ASSERT_EQ(lcd.pixels_written_, 3u);
    const uint16_t* fb = lcd.framebuffer();
    ASSERT_EQ(fb[0], 0xF800);
    ASSERT_EQ(fb[1], 0x07E0);
    ASSERT_EQ(fb[2], 0x001F);
}

void test_spim_to_st7789_large_block() {
    RAM ram(65536);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    SPIM spim;
    ST7789 lcd;
    bool is_data = false;
    spim.set_bus(&bus);
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len, uint8_t*, uint32_t) {
        lcd.set_dc(is_data);
        lcd.spi_transfer(tx, tx_len);
    });

    auto send_cmd = [&](uint8_t cmd) {
        bus.write8(0x20000000, cmd);
        is_data = false;
        spim.write(0x544, Width::Word, 0x20000000);
        spim.write(0x548, Width::Word, 1);
        spim.write(0x538, Width::Word, 0);
        spim.write(0x010, Width::Word, 1);
    };
    auto send_data = [&](uint32_t addr, uint32_t len) {
        is_data = true;
        spim.write(0x544, Width::Word, addr);
        spim.write(0x548, Width::Word, len);
        spim.write(0x538, Width::Word, 0);
        spim.write(0x010, Width::Word, 1);
    };

    spim.write(0x500, Width::Word, 7);

    // Full row: 240 pixels = 480 bytes
    send_cmd(0x2A);
    uint8_t caset[] = {0, 0, 0, 239};
    for (int i = 0; i < 4; i++) bus.write8(0x20000800 + i, caset[i]);
    send_data(0x20000800, 4);

    send_cmd(0x2B);
    uint8_t raset[] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) bus.write8(0x20000800 + i, raset[i]);
    send_data(0x20000800, 4);

    // Write 240 white pixels (0xFFFF)
    for (int i = 0; i < 480; i++)
        bus.write8(0x20001000 + i, 0xFF);

    send_cmd(0x2C);
    send_data(0x20001000, 480);

    ASSERT_EQ(lcd.pixels_written_, 240u);
    const uint16_t* fb = lcd.framebuffer();
    for (int i = 0; i < 240; i++)
        ASSERT_EQ(fb[i], 0xFFFF);
}

// ============================================================
// TWIM → I2C device DMA pipeline (InfiniTime sensor reads)
// ============================================================

void test_twim_read_cst816s_chip_id() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    TWIM twim;
    CST816S cst;
    twim.set_bus(&bus);
    twim.attach(CST816S::I2C_ADDR, &cst);

    // Write register pointer 0xA7 via DMA TX
    bus.write8(0x20000000, 0xA7);
    twim.write(0x588, Width::Word, CST816S::I2C_ADDR);
    twim.write(0x544, Width::Word, 0x20000000);
    twim.write(0x548, Width::Word, 1);
    twim.write(0x200, Width::Word, 1u << 7); // LASTTX_STARTRX
    twim.write(0x534, Width::Word, 0x20000100);
    twim.write(0x538, Width::Word, 1);
    twim.write(0x008, Width::Word, 1); // STARTTX → auto STARTRX

    ASSERT_EQ(bus.read8(0x20000100), 0xB4); // ChipID
}

void test_twim_read_hrs3300_ppg() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    TWIM twim;
    HRS3300 hrs;
    uint64_t cycles = 64000000ULL;
    hrs.set_cycles(&cycles);
    twim.set_bus(&bus);
    twim.attach(HRS3300::I2C_ADDR, &hrs);

    // Enable sensor
    bus.write8(0x20000000, 0x01);
    bus.write8(0x20000001, 0xD0);
    twim.write(0x588, Width::Word, HRS3300::I2C_ADDR);
    twim.write(0x544, Width::Word, 0x20000000);
    twim.write(0x548, Width::Word, 2);
    twim.write(0x008, Width::Word, 1);

    // Write reg pointer 0x09
    bus.write8(0x20000000, 0x09);
    twim.write(0x588, Width::Word, HRS3300::I2C_ADDR);
    twim.write(0x544, Width::Word, 0x20000000);
    twim.write(0x548, Width::Word, 1);
    twim.write(0x200, Width::Word, 1u << 7); // LASTTX_STARTRX
    twim.write(0x534, Width::Word, 0x20000100);
    twim.write(0x538, Width::Word, 7); // Read 7 bytes (0x09..0x0F)
    twim.write(0x008, Width::Word, 1);

    // Decode PPG
    uint8_t m = bus.read8(0x20000100);
    uint8_t h = bus.read8(0x20000101);
    uint8_t l = bus.read8(0x20000106);
    uint32_t ppg = (m << 8) | ((h & 0xF) << 4) | (l & 0xF) | ((l & 0x30) << 12);
    ASSERT(ppg > 0);
    ASSERT(ppg < 262144);
}

void test_twim_read_bma4xx_accel() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    TWIM twim;
    BMA4xx bma;
    bma.set_accel(100, -200, 4096);
    twim.set_bus(&bus);
    twim.attach(BMA4xx::I2C_ADDR, &bma);

    // Write reg pointer 0x12
    bus.write8(0x20000000, 0x12);
    twim.write(0x588, Width::Word, BMA4xx::I2C_ADDR);
    twim.write(0x544, Width::Word, 0x20000000);
    twim.write(0x548, Width::Word, 1);
    twim.write(0x200, Width::Word, 1u << 7);
    twim.write(0x534, Width::Word, 0x20000100);
    twim.write(0x538, Width::Word, 6);
    twim.write(0x008, Width::Word, 1);

    int16_t x = bus.read8(0x20000100) | (bus.read8(0x20000101) << 8);
    int16_t y = bus.read8(0x20000102) | (bus.read8(0x20000103) << 8);
    int16_t z = bus.read8(0x20000104) | (bus.read8(0x20000105) << 8);
    ASSERT_EQ(x, 100);
    ASSERT_EQ((uint16_t)y, (uint16_t)-200);
    ASSERT_EQ(z, 4096);
}

// ============================================================
// Touch → GPIO → GPIOTE → NVIC IRQ chain (InfiniTime touch input)
// ============================================================

void test_touch_irq_chain() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    CST816S cst;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6); // GPIOTE IRQ = 6

    // PineTime: touch IRQ on pin 28, active low
    // Pin 28 idles high (no touch)
    gpio.set_pin(28, true);

    // Set SENSE=Low on pin 28
    gpio.write(0x700 + 28*4, Width::Word, 0x00030000); // SENSE=Low

    // Clear PORT event from initial set_pin
    gpiote.write(0x17C, Width::Word, 0);
    nvic.clear_pending(6);

    // Enable PORT event interrupt
    gpiote.write(0x304, Width::Word, 1u << 31);

    // Enable IRQ 6 in NVIC
    nvic.write(0x100, Width::Word, 1u << 6);

    // CST816S fires IRQ (active low = pin goes low on touch)
    cst.set_irq_callback([&](bool touched) {
        gpio.set_pin(28, !touched); // Active low
    });

    // Simulate touch
    cst.set_touch(true, 120, 120, 0x01);

    // Pin 28 went low → SENSE=Low triggered
    ASSERT(gpio.read(0x520, Width::Word) & (1u << 28)); // LATCH set
    ASSERT_EQ(gpiote.get_ev_port(), 1u);
    ASSERT(nvic.is_pending(6));
    ASSERT(nvic.is_enabled(6));
}

void test_touch_release_irq_chain() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    CST816S cst;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);

    // Pin 28 idles high
    gpio.set_pin(28, true);
    gpio.write(0x700 + 28*4, Width::Word, 0x00030000); // SENSE=Low
    gpiote.write(0x17C, Width::Word, 0);
    nvic.clear_pending(6);

    gpiote.write(0x304, Width::Word, 1u << 31);
    nvic.write(0x100, Width::Word, 1u << 6);

    cst.set_irq_callback([&](bool touched) {
        gpio.set_pin(28, !touched);
    });

    // Touch down
    cst.set_touch(true, 100, 100);
    ASSERT(nvic.is_pending(6));

    // Simulate ISR: clear pending, clear PORT event, read PIN_CNF (auto-toggles SENSE)
    nvic.clear_pending(6);
    gpiote.write(0x17C, Width::Word, 0);
    gpio.write(0x520, Width::Word, 1u << 28); // Clear latch
    gpio.read(0x700 + 28*4, Width::Word); // Auto-toggle SENSE: Low→High

    ASSERT_EQ((gpio.pin_cnf(28) >> 16) & 3, 2u);

    // Touch release: pin goes high → SENSE=High matches
    cst.set_touch(false, 0, 0);
    ASSERT(gpio.read(0x520, Width::Word) & (1u << 28));
    ASSERT(nvic.is_pending(6));
}

void test_touch_multiple_gestures() {
    GPIO gpio;
    GPIOTE gpiote;
    NVIC nvic;
    CST816S cst;
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    TWIM twim;
    twim.set_bus(&bus);
    twim.attach(CST816S::I2C_ADDR, &cst);
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    gpio.set_gpiote(&gpiote);
    gpiote.set_gpio(&gpio);
    gpiote.set_nvic(&nvic, 6);

    // Pin 28 idles high
    gpio.set_pin(28, true);
    gpio.write(0x700 + 28*4, Width::Word, 0x00030000);
    gpiote.write(0x17C, Width::Word, 0);
    nvic.clear_pending(6);

    gpiote.write(0x304, Width::Word, 1u << 31);
    nvic.write(0x100, Width::Word, 1u << 6);

    cst.set_irq_callback([&](bool touched) {
        gpio.set_pin(28, !touched);
    });

    uint8_t gestures[] = {0x01, 0x02, 0x03, 0x04}; // Up, Down, Left, Right
    for (auto g : gestures) {
        // Touch
        cst.set_touch(true, 120, 120, g);
        ASSERT(nvic.is_pending(6));

        // Simulate ISR: read gesture via TWIM
        bus.write8(0x20000000, 0x01); // Gesture register
        twim.write(0x588, Width::Word, CST816S::I2C_ADDR);
        twim.write(0x544, Width::Word, 0x20000000);
        twim.write(0x548, Width::Word, 1);
        twim.write(0x200, Width::Word, 1u << 7);
        twim.write(0x534, Width::Word, 0x20000100);
        twim.write(0x538, Width::Word, 1);
        twim.write(0x008, Width::Word, 1);
        ASSERT_EQ(bus.read8(0x20000100), g);

        // Clean up for next gesture: read PIN_CNF (auto-toggles SENSE)
        nvic.clear_pending(6);
        gpiote.write(0x17C, Width::Word, 0);
        gpio.write(0x520, Width::Word, 1u << 28);
        gpio.read(0x700 + 28*4, Width::Word); // Auto-toggle SENSE

        // Release
        cst.set_touch(false, 0, 0);
        nvic.clear_pending(6);
        gpiote.write(0x17C, Width::Word, 0);
        gpio.write(0x520, Width::Word, 1u << 28);
        gpio.read(0x700 + 28*4, Width::Word); // Auto-toggle back
    }
}

// ============================================================
// SPIM with NVIC IRQ (InfiniTime display DMA completion)
// ============================================================

void test_spim_irq_end_event() {
    RAM ram(4096);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    SPIM spim;
    ST7789 lcd;
    bool is_data = true;
    spim.set_bus(&bus);
    spim.set_nvic(&nvic, 3); // SPIM0 IRQ = 3
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len, uint8_t*, uint32_t) {
        lcd.set_dc(is_data);
        lcd.spi_transfer(tx, tx_len);
    });

    spim.write(0x500, Width::Word, 7);
    spim.write(0x304, Width::Word, 1u << 6); // INTENSET END

    // DMA transfer
    bus.write8(0x20000000, 0xF8);
    bus.write8(0x20000001, 0x00);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 2);
    spim.write(0x534, Width::Word, 0x20000100);
    spim.write(0x538, Width::Word, 0);
    spim.write(0x010, Width::Word, 1);

    ASSERT(nvic.is_pending(3));
    ASSERT_EQ(spim.read(0x118, Width::Word), 1u); // END event
}

// ============================================================
// TWIM with IRQ (InfiniTime sensor DMA completion)
// ============================================================

void test_twim_irq_stopped() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    TWIM twim;
    CST816S cst;
    twim.set_bus(&bus);
    twim.set_nvic(&nvic, 4);
    twim.attach(CST816S::I2C_ADDR, &cst);

    twim.write(0x304, Width::Word, 1u << 1); // INTENSET STOPPED
    twim.write(0x200, Width::Word, (1u << 9) | (1u << 7)); // LASTTX_STOP + LASTTX_STARTRX
    twim.write(0x588, Width::Word, CST816S::I2C_ADDR);
    // Just stop
    twim.write(0x014, Width::Word, 1);
    ASSERT(nvic.is_pending(4));
}

// ============================================================
// SysTick as FreeRTOS tick source
// ============================================================

void test_systick_periodic_interrupt() {
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    // Configure SysTick: RVR=64000 (1ms at 64MHz), enable + interrupt
    nvic.write(0x014, Width::Word, 64000 - 1); // SYST_RVR
    nvic.write(0x018, Width::Word, 0);          // SYST_CVR (write clears)
    nvic.write(0x010, Width::Word, 0x07);       // ENABLE + TICKINT + CLKSOURCE

    // Tick enough cycles for CVR to reach 0
    for (uint64_t cy = 1; cy <= 64001; cy++)
        nvic.tick(cy);

    // SysTick exception = 15
    ASSERT(nvic.is_sys_pending(15));
}

void test_systick_reload() {
    NVIC nvic;
    nvic.write(0x014, Width::Word, 100); // RVR=100
    nvic.write(0x018, Width::Word, 0);
    nvic.write(0x010, Width::Word, 0x07);

    // Tick 102 cycles (first tick sets CVR=100, then counts down)
    for (uint64_t cy = 1; cy <= 102; cy++)
        nvic.tick(cy);

    ASSERT(nvic.is_sys_pending(15));
}

// ============================================================
// GPIO chip-select for SPIM (PineTime LCD CS management)
// ============================================================

void test_gpio_cs_for_spi() {
    GPIO gpio;
    // Pin 25 = LCD CS (active low)
    gpio.write(0x514, Width::Word, 1u << 25); // Pin 25 = output
    gpio.write(0x508, Width::Word, 1u << 25); // OUTSET: CS high (deasserted)
    ASSERT(gpio.get_pin(25));

    // Assert CS
    gpio.write(0x50C, Width::Word, 1u << 25); // OUTCLR: CS low
    ASSERT(!gpio.get_pin(25));

    // Deassert CS
    gpio.write(0x508, Width::Word, 1u << 25); // OUTSET: CS high
    ASSERT(gpio.get_pin(25));
}

// ============================================================
// SPIM legacy SPI FIFO behavior (SoftDevice BLE stack)
// ============================================================

void test_legacy_spi_fifo_pipeline() {
    SPIM spim;
    uint8_t responses[4] = {0};
    int resp_idx = 0;
    spim.set_callback([&](const uint8_t* tx, uint32_t, uint8_t* rx, uint32_t) {
        rx[0] = tx[0] + 1; // Echo back tx+1
    });

    // Pipeline 4 transfers
    for (int i = 0; i < 4; i++)
        spim.write(0x51C, Width::Word, i); // Legacy TXD

    // Should have 4 READY events
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(spim.read(0x108, Width::Word), 1u); // READY
        responses[resp_idx++] = spim.read(0x518, Width::Word); // RXD
        spim.write(0x108, Width::Word, 0); // Clear READY
    }

    ASSERT_EQ(responses[0], 1u);
    ASSERT_EQ(responses[1], 2u);
    ASSERT_EQ(responses[2], 3u);
    ASSERT_EQ(responses[3], 4u);
}

// ============================================================
// SPIM → SPI Flash DMA (MCUBoot firmware verification)
// ============================================================

void test_spim_to_spi_flash_read() {
    RAM ram(4096);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    SPIM spim;
    SpiFlash flash(4 * 1024 * 1024); // 4MB
    // Write test pattern at flash address 0x1000
    flash.data()[0x1000] = 0xAA;
    flash.data()[0x1001] = 0xBB;
    flash.data()[0x1002] = 0xCC;
    flash.data()[0x1003] = 0xDD;

    spim.set_bus(&bus);
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len,
                          uint8_t* rx, uint32_t rx_len) {
        flash.transfer(tx, tx_len, rx, rx_len);
    });

    spim.write(0x500, Width::Word, 7); // ENABLE

    // Send READ command: 0x03, addr[2:0] = 0x00, 0x10, 0x00
    bus.write8(0x20000000, 0x03); // READ
    bus.write8(0x20000001, 0x00); // Addr MSB
    bus.write8(0x20000002, 0x10); // Addr mid
    bus.write8(0x20000003, 0x00); // Addr LSB
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 4); // TX: cmd + 3 addr bytes
    spim.write(0x534, Width::Word, 0x20000100);
    spim.write(0x538, Width::Word, 8); // RX: 4 dummy + 4 data
    spim.write(0x010, Width::Word, 1);

    ASSERT_EQ(bus.read8(0x20000104), 0xAA);
    ASSERT_EQ(bus.read8(0x20000105), 0xBB);
    ASSERT_EQ(bus.read8(0x20000106), 0xCC);
    ASSERT_EQ(bus.read8(0x20000107), 0xDD);
}

void test_spim_to_spi_flash_jedec_id() {
    RAM ram(4096);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    SPIM spim;
    SpiFlash flash(4096);
    spim.set_bus(&bus);
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len,
                          uint8_t* rx, uint32_t rx_len) {
        flash.transfer(tx, tx_len, rx, rx_len);
    });

    spim.write(0x500, Width::Word, 7);

    // JEDEC ID: 0x9F + 3 dummy
    bus.write8(0x20000000, 0x9F);
    bus.write8(0x20000001, 0x00);
    bus.write8(0x20000002, 0x00);
    bus.write8(0x20000003, 0x00);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 4);
    spim.write(0x534, Width::Word, 0x20000100);
    spim.write(0x538, Width::Word, 4);
    spim.write(0x010, Width::Word, 1);

    // JEDEC ID: manufacturer=0x0B, mem_type=0x40, capacity=0x16
    ASSERT_EQ(bus.read8(0x20000101), 0x0B);
    ASSERT_EQ(bus.read8(0x20000102), 0x40);
    ASSERT_EQ(bus.read8(0x20000103), 0x16);
}

void test_spim_to_spi_flash_page_program() {
    RAM ram(4096);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    SPIM spim;
    SpiFlash flash(65536);
    spim.set_bus(&bus);
    spim.set_callback([&](const uint8_t* tx, uint32_t tx_len,
                          uint8_t* rx, uint32_t rx_len) {
        flash.transfer(tx, tx_len, rx, rx_len);
    });

    spim.write(0x500, Width::Word, 7);

    // WREN
    bus.write8(0x20000000, 0x06);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 1);
    spim.write(0x538, Width::Word, 0);
    spim.write(0x010, Width::Word, 1);

    // PAGE PROGRAM at 0x0000: cmd + addr + 4 bytes data
    bus.write8(0x20000000, 0x02);
    bus.write8(0x20000001, 0x00);
    bus.write8(0x20000002, 0x00);
    bus.write8(0x20000003, 0x00);
    bus.write8(0x20000004, 0x11);
    bus.write8(0x20000005, 0x22);
    bus.write8(0x20000006, 0x33);
    bus.write8(0x20000007, 0x44);
    spim.write(0x544, Width::Word, 0x20000000);
    spim.write(0x548, Width::Word, 8);
    spim.write(0x538, Width::Word, 0);
    spim.write(0x010, Width::Word, 1);

    ASSERT_EQ(flash.read(0, Width::Byte), 0x11);
    ASSERT_EQ(flash.read(1, Width::Byte), 0x22);
    ASSERT_EQ(flash.read(2, Width::Byte), 0x33);
    ASSERT_EQ(flash.read(3, Width::Byte), 0x44);
}

// ============================================================
// Timer → NVIC IRQ (FreeRTOS timer peripheral)
// ============================================================

void test_timer_compare_irq_to_nvic() {
    NVIC nvic;
    Timer timer;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    timer.set_nvic(&nvic, 8); // TIMER0 IRQ = 8

    // Enable IRQ 8
    nvic.write(0x100, Width::Word, 1u << 8);

    timer.write(0x540, Width::Word, 3); // CC[0] = 3
    timer.write(0x510, Width::Word, 0); // PRESCALER=0
    timer.write(0x508, Width::Word, 3); // 32-bit
    timer.write(0x304, Width::Word, 1u << 16); // INTENSET COMPARE0
    timer.write(0x000, Width::Word, 1); // START

    for (uint64_t cy = 0; cy <= 16; cy++)
        timer.tick(cy);

    ASSERT(nvic.is_pending(8));
    ASSERT(nvic.is_enabled(8));
    ASSERT_EQ(nvic.get_pending_irq(), 8);
}

// ============================================================
// RTC → NVIC IRQ (InfiniTime RTC tick)
// ============================================================

void test_rtc_tick_irq_to_nvic() {
    NVIC nvic;
    RTC rtc;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);
    rtc.set_nvic(&nvic, 17); // RTC1 IRQ = 17

    nvic.write(0x100, Width::Word, 1u << 17);

    rtc.write(0x508, Width::Word, 0); // PRESCALER=0
    rtc.write(0x304, Width::Word, 1u << 0); // INTENSET TICK
    rtc.write(0x000, Width::Word, 1); // START

    for (uint64_t cy = 0; cy < 1953 * 2; cy++)
        rtc.tick(cy);

    ASSERT(nvic.is_pending(17));
    ASSERT(nvic.is_enabled(17));
}

int main() {
    // SPIM → ST7789
    RUN(test_spim_to_st7789_cmd);
    RUN(test_spim_to_st7789_pixel_dma);
    RUN(test_spim_to_st7789_large_block);
    // TWIM → I2C devices
    RUN(test_twim_read_cst816s_chip_id);
    RUN(test_twim_read_hrs3300_ppg);
    RUN(test_twim_read_bma4xx_accel);
    // Touch → GPIO → GPIOTE → NVIC
    RUN(test_touch_irq_chain);
    RUN(test_touch_release_irq_chain);
    RUN(test_touch_multiple_gestures);
    // SPIM + NVIC IRQ
    RUN(test_spim_irq_end_event);
    // TWIM + NVIC IRQ
    RUN(test_twim_irq_stopped);
    // SysTick tick
    RUN(test_systick_periodic_interrupt);
    RUN(test_systick_reload);
    // GPIO CS
    RUN(test_gpio_cs_for_spi);
    // Legacy SPI FIFO
    RUN(test_legacy_spi_fifo_pipeline);
    // SPIM → SPI Flash
    RUN(test_spim_to_spi_flash_read);
    RUN(test_spim_to_spi_flash_jedec_id);
    RUN(test_spim_to_spi_flash_page_program);
    // Timer → NVIC
    RUN(test_timer_compare_irq_to_nvic);
    // RTC → NVIC
    RUN(test_rtc_tick_irq_to_nvic);
    TEST_REPORT();
}
