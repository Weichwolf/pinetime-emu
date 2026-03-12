#include "bus.hpp"
#include "cpu.hpp"
#include "device/memory.hpp"
#include "device/nvic.hpp"
#include "device/clock_power.hpp"
#include "device/gpio.hpp"
#include "device/timer.hpp"
#include "device/spim.hpp"
#include "device/twim.hpp"
#include "device/nvmc.hpp"
#include "device/wdt.hpp"
#include "device/st7789.hpp"
#include "device/i2c_devices.hpp"
#include "device/rng.hpp"
#include "device/ecb.hpp"
#include "device/uarte.hpp"
#include "device/radio.hpp"
#include "device/ppi.hpp"
#include "device/saadc.hpp"
#include "device/temp.hpp"
#include "device/spi_flash.hpp"
#include "device/egu.hpp"
#include "device/ble_hci.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// nRF52832 / PineTime memory map
static constexpr uint32_t FLASH_BASE = 0x00000000;
static constexpr uint32_t FLASH_SIZE = 512 * 1024;
static constexpr uint32_t SRAM_BASE  = 0x20000000;
static constexpr uint32_t SRAM_SIZE  = 64 * 1024;
static constexpr uint32_t SCS_BASE   = 0xE000E000;
static constexpr uint32_t SCS_SIZE   = 0x1000;

static constexpr int DISPLAY_W = 240;
static constexpr int DISPLAY_H = 240;

// FICR / UICR
static constexpr uint32_t FICR_BASE = 0x10000000;
static constexpr uint32_t FICR_SIZE = 0x1000;
static constexpr uint32_t UICR_BASE = 0x10001000;
static constexpr uint32_t UICR_SIZE = 0x1000;

// Stub device
struct StubDevice : pt::Device {
    uint32_t read(uint32_t, pt::Width) override { return 0; }
    void write(uint32_t, pt::Width, uint32_t) override {}
};

// DWT (Data Watchpoint and Trace) — CYCCNT at offset 0x004
struct DWT : pt::Device {
    uint32_t ctrl_ = 0;
    pt::CPU* cpu_ = nullptr;
    void set_cpu(pt::CPU* c) { cpu_ = c; }
    uint32_t read(uint32_t addr, pt::Width) override {
        switch (addr) {
        case 0x000: return ctrl_;
        case 0x004: return cpu_ ? static_cast<uint32_t>(cpu_->cycles) : 0; // CYCCNT
        }
        return 0;
    }
    void write(uint32_t addr, pt::Width, uint32_t val) override {
        switch (addr) {
        case 0x000: ctrl_ = val; break;
        case 0x004: break; // CYCCNT write (typically to clear)
        }
    }
};

// FICR: Factory Information Configuration Registers
struct FICR : pt::Device {
    uint32_t read(uint32_t addr, pt::Width) override {
        switch (addr) {
        case 0x010: return 0x1000;         // CODEPAGESIZE = 4KB
        case 0x014: return 128;            // CODESIZE = 128 pages
        case 0x060: return 0x00000034;     // CONFIGID
        case 0x064: return 0x00052832;     // DEVICEID[0]
        case 0x068: return 0x12345678;     // DEVICEID[1]
        case 0x0A0: return 0x00052832;     // INFO.PART = nRF52832
        case 0x0A4: return 0x41414230;     // INFO.VARIANT = "B0AA" (QFAA rev B0)
        case 0x0A8: return 0x00002000;     // INFO.PACKAGE = QFN48
        case 0x0AC: return 64;             // INFO.RAM = 64KB
        case 0x0B0: return 512;            // INFO.FLASH = 512KB
        }
        return 0xFFFFFFFF;
    }
    void write(uint32_t, pt::Width, uint32_t) override {}
};

// PineTime GPIO pin assignments
namespace PineDef {
    constexpr uint8_t LCD_SCK     = 2;
    constexpr uint8_t LCD_MOSI    = 3;
    constexpr uint8_t LCD_CS      = 25;
    constexpr uint8_t LCD_DC      = 18;
    constexpr uint8_t LCD_RST     = 26;
    constexpr uint8_t LCD_BL_LO   = 14;
    constexpr uint8_t LCD_BL_MID  = 22;
    constexpr uint8_t LCD_BL_HI   = 23;
    constexpr uint8_t BUTTON      = 13;
    constexpr uint8_t CHARGE_IND  = 12;
    constexpr uint8_t POWER_PRES  = 19;
    constexpr uint8_t MOTOR       = 16;
    constexpr uint8_t TWI_SDA     = 6;
    constexpr uint8_t TWI_SCL     = 7;
    constexpr uint8_t TOUCH_RST   = 10;
    constexpr uint8_t TOUCH_IRQ   = 28;
    constexpr uint8_t SPI_FLASH_CS = 5;
}

struct EmulatorContext {
    pt::Flash flash{FLASH_SIZE};
    pt::RAM sram{SRAM_SIZE};
    pt::NVIC nvic;
    FICR ficr;
    StubDevice uicr_stub;

    // nRF52 peripherals
    pt::ClockPower clock_power;
    pt::GPIO gpio;
    pt::GPIOTE gpiote;
    pt::Timer timer0, timer1, timer2;
    pt::RTC rtc0, rtc1, rtc2;
    pt::SPIM spim0;        // LCD
    pt::TWIM twim1;        // Sensors
    pt::NVMC nvmc;
    pt::WDT wdt;
    pt::Radio radio;
    pt::PPI ppi;
    pt::SAADC saadc;
    pt::TEMP temp;
    pt::RNG rng;
    pt::ECB ecb;
    pt::UARTE uarte0;
    StubDevice fpu_stub;
    StubDevice ccm_stub;
    pt::EGU egu0, egu1, egu2, egu3, egu4, egu5;
    StubDevice stub_device;
    DWT dwt;
    pt::BleHci ble_hci;

    // PineTime devices
    pt::ST7789 display;
    pt::CST816S touch;
    pt::HRS3300 hrs;
    pt::BMA4xx bma;

    // External SPI flash (4MB, memory-mapped at 0x04000000)
    static constexpr uint32_t EXT_FLASH_MMAP = 0x04000000;
    pt::SpiFlash ext_flash{4 * 1024 * 1024};

    pt::Bus bus;
    pt::CPU cpu{&bus};

    EmulatorContext() {
        // Memory
        bus.map(FLASH_BASE, FLASH_SIZE, &flash);
        bus.map(SRAM_BASE, SRAM_SIZE, &sram);
        bus.map(SCS_BASE, SCS_SIZE, &nvic);
        bus.map(FICR_BASE, FICR_SIZE, &ficr);
        bus.map(UICR_BASE, UICR_SIZE, &uicr_stub);

        // nRF52 peripherals
        bus.map(0x40000000, 0x1000, &clock_power); // CLOCK + POWER
        bus.map(0x40001000, 0x1000, &radio);        // RADIO
        bus.map(0x40002000, 0x1000, &uarte0);      // UARTE0
        bus.map(0x40003000, 0x1000, &spim0);       // SPI0/TWI0 — used as SPI for LCD
        bus.map(0x40004000, 0x1000, &twim1);       // SPI1/TWI1 — used as TWI for sensors
        bus.map(0x40006000, 0x1000, &gpiote);      // GPIOTE
        bus.map(0x40007000, 0x1000, &saadc);        // SAADC
        bus.map(0x40008000, 0x1000, &timer0);      // TIMER0
        bus.map(0x40009000, 0x1000, &timer1);      // TIMER1
        bus.map(0x4000A000, 0x1000, &timer2);      // TIMER2
        bus.map(0x4000B000, 0x1000, &rtc0);        // RTC0
        bus.map(0x4000C000, 0x1000, &temp);         // TEMP
        bus.map(0x4000D000, 0x1000, &rng);         // RNG
        radio.set_nvic(&nvic, 1);
        radio.set_bus(&bus);
        radio.set_ppi(&ppi);
        radio.set_cycles(&cpu.cycles, &cpu.event_reg);
        ble_hci.set_bus(&bus);
        ble_hci.set_radio(&radio);
        uarte0.set_bus(&bus);
        uarte0.set_nvic(&nvic, 2);  // UARTE0 = IRQ 2
        uarte0.set_tx_callback([](const char* data, size_t len) {
            fprintf(stderr, "[UART] ");
            fwrite(data, 1, len, stderr);
            if (len > 0 && data[len-1] != '\n') fprintf(stderr, "\n");
        });
        ppi.set_bus(&bus);
        saadc.set_bus(&bus);
        saadc.set_nvic(&nvic, 7);
        temp.set_nvic(&nvic, 12);
        rng.set_nvic(&nvic, 13);
        bus.map(0x4000E000, 0x1000, &ecb);         // ECB
        ecb.set_bus(&bus);
        bus.map(0x4000F000, 0x1000, &ccm_stub);    // CCM/AAR
        bus.map(0x40010000, 0x1000, &wdt);         // WDT
        wdt.set_nvic(&nvic, 16);                    // WDT = IRQ 16
        bus.map(0x40011000, 0x1000, &rtc1);        // RTC1
        bus.map(0x4001E000, 0x1000, &nvmc);        // NVMC
        bus.map(0x40014000, 0x1000, &egu0);    // SWI0/EGU0
        bus.map(0x40015000, 0x1000, &egu1);    // SWI1/EGU1
        bus.map(0x40016000, 0x1000, &egu2);    // SWI2/EGU2
        bus.map(0x40017000, 0x1000, &egu3);    // SWI3/EGU3
        bus.map(0x40018000, 0x1000, &egu4);    // SWI4/EGU4
        bus.map(0x40019000, 0x1000, &egu5);    // SWI5/EGU5
        egu0.set_nvic(&nvic, 20);
        egu1.set_nvic(&nvic, 21);
        egu2.set_nvic(&nvic, 22);
        egu3.set_nvic(&nvic, 23);
        egu4.set_nvic(&nvic, 24);
        egu5.set_nvic(&nvic, 25);
        bus.map(0x4001F000, 0x1000, &ppi);            // PPI (at BPROT addr for nRF52)
        bus.map(0x40020000, 0x1000, &stub_device);  // Reserved
        bus.map(0x40024000, 0x1000, &rtc2);        // RTC2
        bus.map(0x4002A000, 0x1000, &fpu_stub);    // FPU access port
        bus.map(0x50000000, 0x1000, &gpio);         // GPIO P0
        bus.map(EXT_FLASH_MMAP, ext_flash.size(), &ext_flash); // External SPI flash memory-mapped
        bus.map(0xE0001000, 0x1000, &dwt);           // DWT (Data Watchpoint and Trace)
        bus.map(0xF0000000, 0x1000, &stub_device);   // CoreSight ROM Table

        bus.set_fast_path(flash.data(), FLASH_SIZE,
                          sram.data(), SRAM_BASE, SRAM_SIZE);
        cpu.set_flash(flash.data(), FLASH_SIZE);

        // Wire CPU to NVIC and DWT
        cpu.set_nvic(&nvic);
        nvic.set_irq_pending_flag(&cpu.irq_pending);
        nvic.set_event_reg(&cpu.event_reg);
        nvic.set_cpu_pc(&cpu.r[15]);
        bus.cpu_pc_ = &cpu.r[15];
        bus.cpu_cy_ = &cpu.cycles;
        bus.watch_addr_ = 0x2000F187;  // Watch phy_transition in g_ble_phy_data
        dwt.set_cpu(&cpu);
        flash.set_on_write([this](uint32_t addr, uint32_t len) {
            cpu.invalidate_cache(addr, len);
        });
        nvmc.set_cpu(&cpu);

        // Wire peripherals to NVIC (nRF52 IRQ numbers)
        clock_power.set_nvic(&nvic, 0);
        clock_power.set_ppi(&ppi);
        timer0.set_nvic(&nvic, 8); timer0.set_cycles(&cpu.cycles, &cpu.r[15]);
        timer0.set_ppi(&ppi, 0x40008000);
        timer1.set_nvic(&nvic, 9); timer1.set_cycles(&cpu.cycles, &cpu.r[15]);
        timer1.set_ppi(&ppi, 0x40009000);
        timer2.set_nvic(&nvic, 10); timer2.set_cycles(&cpu.cycles, &cpu.r[15]);
        timer2.set_ppi(&ppi, 0x4000A000);
        rtc0.set_nvic(&nvic, 11);
        rtc0.set_ppi(&ppi, 0x4000B000);
        rtc1.set_nvic(&nvic, 17);
        rtc1.set_ppi(&ppi, 0x40011000);
        rtc2.set_nvic(&nvic, 36);
        rtc2.set_ppi(&ppi, 0x40024000);

        // Wire SPIM/TWIM to bus (for DMA) and NVIC
        spim0.set_bus(&bus);
        spim0.set_nvic(&nvic, 3);
        twim1.set_bus(&bus);
        twim1.set_nvic(&nvic, 4);

        // Wire GPIOTE to GPIO and NVIC
        gpio.set_gpiote(&gpiote);
        gpiote.set_gpio(&gpio);
        gpiote.set_nvic(&nvic, 6);

        // Wire NVMC to flash
        nvmc.set_flash(&flash);
        flash.set_nvmc_config(nvmc.config_ptr());

        // Connect PineTime display to SPIM0
        spim0.set_callback([this](const uint8_t* tx, uint32_t tx_len,
                                   uint8_t* rx, uint32_t rx_len) {
            bool lcd_cs = gpio.get_pin(PineDef::LCD_CS);
            bool flash_cs = gpio.get_pin(PineDef::SPI_FLASH_CS);
            if (!flash_cs) {
                ext_flash.transfer(tx, tx_len, rx, rx_len);
            }
            if (!lcd_cs) {
                bool dc = gpio.get_pin(PineDef::LCD_DC);
                display.set_dc(dc);
                display.spi_transfer(tx, tx_len);
            }
        });

        // Connect I2C devices to TWIM1
        touch.set_irq_callback([this](bool active) {
            gpio.set_pin(PineDef::TOUCH_IRQ, !active); // Active low
        });
        twim1.attach(pt::CST816S::I2C_ADDR, &touch);
        hrs.set_cycles(&cpu.cycles);
        bma.set_cycles(&cpu.cycles);
        twim1.attach(pt::HRS3300::I2C_ADDR, &hrs);
        twim1.attach(pt::BMA4xx::I2C_ADDR, &bma);

        // Set PineTime default input pin states
        gpio.set_pin(PineDef::CHARGE_IND, true);  // Not charging
        gpio.set_pin(PineDef::TOUCH_IRQ, true);   // Touch IRQ idle (active low)
        gpio.set_pin(PineDef::POWER_PRES, false);  // No USB power
        gpio.set_pin(PineDef::BUTTON, false);       // Button not pressed

        // Reset SPI flash state when CS pin goes high (deselected)
        gpio.set_on_out_change([this](uint32_t old_out, uint32_t new_out) {
            uint32_t rising = new_out & ~old_out; // pins that went high
            if (rising & (1u << PineDef::SPI_FLASH_CS))
                ext_flash.reset_cmd();
        });

        init_tickable();
    }

    // All tickable peripherals
    static constexpr int NUM_TICKABLE = 12;
    pt::Device* tickable_[NUM_TICKABLE];
    int num_tickable_ = 0;

    void init_tickable() {
        tickable_[num_tickable_++] = &nvic;
        tickable_[num_tickable_++] = &timer0;
        tickable_[num_tickable_++] = &timer1;
        tickable_[num_tickable_++] = &timer2;
        tickable_[num_tickable_++] = &rtc0;
        tickable_[num_tickable_++] = &rtc1;
        tickable_[num_tickable_++] = &rtc2;
        tickable_[num_tickable_++] = &rng;
        tickable_[num_tickable_++] = &temp;
        tickable_[num_tickable_++] = &ppi;
        tickable_[num_tickable_++] = &radio;
        tickable_[num_tickable_++] = &wdt;
    }

    void tick_peripherals() {
        for (int i = 0; i < num_tickable_; i++)
            tickable_[i]->tick(cpu.cycles);
    }

    // Returns the earliest cycle at which any peripheral needs attention
    uint64_t next_peripheral_event() const {
        uint64_t earliest = UINT64_MAX;
        for (int i = 0; i < num_tickable_; i++) {
            uint64_t ev = tickable_[i]->next_event(cpu.cycles);
            if (ev < earliest) earliest = ev;
        }
        return earliest;
    }
};

// ============================================================================
// Display rendering: direct RGB565 blit
// ============================================================================

static void render_display(SDL_Texture* tex, const pt::ST7789& display) {
    void* pixels;
    int pitch;
    SDL_LockTexture(tex, nullptr, &pixels, &pitch);

    const uint16_t* fb = display.framebuffer();
    if (display.is_on()) {
        for (int y = 0; y < DISPLAY_H; y++)
            memcpy((uint8_t*)pixels + y * pitch, fb + y * DISPLAY_W, DISPLAY_W * 2);
    } else {
        memset(pixels, 0, pitch * DISPLAY_H);
    }

    SDL_UnlockTexture(tex);
}

// ============================================================================
// Run modes
// ============================================================================

static int run_headless(EmulatorContext& emu, uint32_t timeout_ms, bool trace) {
    uint64_t max_cycles = (uint64_t)timeout_ms * 64000;
    uint64_t start = emu.cpu.cycles;
    uint64_t last_report = start;

    int snap_idx = 0;
    uint64_t next_snap = start + 64'000'000;

    uint64_t diag_iter = 0;
    while (emu.cpu.cycles - start < max_cycles && !emu.cpu.halted) {
        if (++diag_iter == 500000) {
            fprintf(stderr, "[DIAG] cy=%.2fM PC=0x%08X wfi=%d\n",
                    emu.cpu.cycles/1e6, emu.cpu.r[15], emu.cpu.wfi);
            for (int di = 0; di < emu.num_tickable_; di++) {
                uint64_t ev = emu.tickable_[di]->next_event(emu.cpu.cycles);
                if (ev < emu.cpu.cycles + 1000000)
                    fprintf(stderr, "  [%d] next_event=%lu (delta=%ld)\n",
                            di, (unsigned long)ev, (long)(ev - emu.cpu.cycles));
            }
        }
        uint64_t next_ev = emu.next_peripheral_event();
        uint64_t limit = emu.cpu.cycles + 10000; // Max batch for responsiveness
        uint64_t target = std::min({next_ev, limit, start + max_cycles});
        if (target <= emu.cpu.cycles) target = emu.cpu.cycles + 1;

        emu.cpu.yield_at = next_ev;
        if (emu.cpu.wfi) {
            emu.cpu.cycles = target;
            emu.tick_peripherals();
            emu.cpu.check_exceptions();
            // Wake from WFE if event_reg was set by peripheral tick
            if (emu.cpu.event_reg) emu.cpu.wfi = false;
        } else {
            emu.cpu.run(target);
            emu.tick_peripherals();
            emu.cpu.check_exceptions();
        }
        emu.ble_hci.poll(emu.cpu.cycles);

        if (emu.cpu.breakpoint_hit) {
            fprintf(stderr, "[BP] PC=0x%08X cy=%.2fM SP=0x%08X LR=0x%08X R0=0x%08X R1=0x%08X R3=0x%08X xPSR=0x%08X prev=0x%08X\n",
                    emu.cpu.r[15], emu.cpu.cycles/1e6, emu.cpu.r[13], emu.cpu.r[14],
                    emu.cpu.r[0], emu.cpu.r[1], emu.cpu.r[3], emu.cpu.xpsr, emu.cpu.prev_pc_);
            emu.cpu.breakpoint_hit = false;
        }
        if (trace && emu.cpu.cycles - last_report >= 1000000) {
            fprintf(stderr, "[TRACE] %8.2fM  PC=0x%08X  SP=0x%08X  LR=0x%08X  xPSR=0x%08X\n",
                    (emu.cpu.cycles - start) / 1e6,
                    emu.cpu.r[15], emu.cpu.r[13], emu.cpu.r[14], emu.cpu.xpsr);
            last_report = emu.cpu.cycles;
        }

        if (emu.cpu.cycles >= next_snap) {
            char fname[32];
            snprintf(fname, sizeof(fname), "snap_%02d.ppm", snap_idx);
            const uint16_t* fb = emu.display.framebuffer();
            FILE* f = fopen(fname, "wb");
            if (f) {
                fprintf(f, "P6\n240 240\n255\n");
                for (int i = 0; i < 240*240; i++) {
                    uint16_t c = fb[i];
                    uint8_t rgb[3] = {
                        (uint8_t)(((c >> 11) & 0x1F) << 3),
                        (uint8_t)(((c >> 5) & 0x3F) << 2),
                        (uint8_t)((c & 0x1F) << 3)
                    };
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
            }
            snap_idx++;
            next_snap += 64'000'000;
        }
    }
    fprintf(stderr, "[EMU] %d snapshots saved (snap_00.ppm .. snap_%02d.ppm)\n",
            snap_idx, snap_idx > 0 ? snap_idx - 1 : 0);

    uint64_t elapsed = emu.cpu.cycles - start;
    fprintf(stderr, "[EMU] %llu cycles (%.2f M), %s\n",
            (unsigned long long)elapsed, elapsed / 1e6,
            emu.cpu.halted ? "HALTED" : "TIMEOUT");
    fprintf(stderr, "[EMU] PC=0x%08X SP=0x%08X LR=0x%08X xPSR=0x%08X\n",
            emu.cpu.r[15], emu.cpu.r[13], emu.cpu.r[14], emu.cpu.xpsr);
    fprintf(stderr, "[EMU] R0-R7:  %08X %08X %08X %08X %08X %08X %08X %08X\n",
            emu.cpu.r[0],emu.cpu.r[1],emu.cpu.r[2],emu.cpu.r[3],
            emu.cpu.r[4],emu.cpu.r[5],emu.cpu.r[6],emu.cpu.r[7]);
    fprintf(stderr, "[EMU] R8-R12: %08X %08X %08X %08X %08X\n",
            emu.cpu.r[8],emu.cpu.r[9],emu.cpu.r[10],emu.cpu.r[11],emu.cpu.r[12]);
    // LFS buffer diagnostic
    {
        uint32_t buf_ptr = emu.bus.read32(0x2000C91C);
        fprintf(stderr, "[LFS] tag_scan buffer@0x%08X, first 32 bytes:\n", buf_ptr);
        for (int i = 0; i < 32; i++) {
            if (i % 16 == 0) fprintf(stderr, "  +%03X:", i);
            fprintf(stderr, " %02X", emu.bus.read8(buf_ptr + i));
            if (i % 16 == 15) fprintf(stderr, "\n");
        }
        fprintf(stderr, "[LFS] SPIM rx buffer @0x20002A08, first 32 bytes:\n");
        for (int i = 0; i < 32; i++) {
            if (i % 16 == 0) fprintf(stderr, "  +%03X:", i);
            fprintf(stderr, " %02X", emu.bus.read8(0x20002A08 + i));
            if (i % 16 == 15) fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "[SPIM0] transfers=%u\n", emu.spim0.transfer_count());
    fprintf(stderr, "[HRS3300] reads=%u writes=%u enable=0x%02X\n",
            emu.hrs.read_count_, emu.hrs.write_count_, emu.hrs.reg(0x01));
    // External SPI flash state
    {
        int non_ff = 0;
        for (uint32_t i = 0; i < emu.ext_flash.size() && i < 0x100000; i++)
            if (emu.ext_flash.data()[i] != 0xFF) non_ff++;
        fprintf(stderr, "[ExtFlash] non-0xFF bytes in first 1MB: %d, reads=%u writes=%u status=%u\n",
                non_ff, emu.ext_flash.read_count_, emu.ext_flash.write_count_, emu.ext_flash.status_count_);
    }
    // Display state + screenshot
    {
        const uint16_t* fb = emu.display.framebuffer();
        int nonzero = 0;
        for (int i = 0; i < 240*240; i++) if (fb[i]) nonzero++;
        fprintf(stderr, "[Display] on=%d, non-zero=%d/%d, cmds=%u data=%uB px_wr=%u px_clip=%u ramwr=%u\n",
                emu.display.is_on() ? 1 : 0, nonzero, 240*240,
                emu.display.cmd_count(), emu.display.data_bytes(),
                emu.display.pixels_written_, emu.display.pixels_clipped_,
                emu.display.ramwr_count_);
        // Dump PPM screenshot
        FILE* f = fopen("screenshot.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n240 240\n255\n");
            for (int i = 0; i < 240*240; i++) {
                uint16_t c = fb[i];
                uint8_t rgb[3] = {
                    (uint8_t)(((c >> 11) & 0x1F) << 3),
                    (uint8_t)(((c >> 5) & 0x3F) << 2),
                    (uint8_t)((c & 0x1F) << 3)
                };
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
            fprintf(stderr, "[Display] Screenshot saved to screenshot.ppm\n");
        }
    }
    // FreeRTOS state dump: scan SRAM for TCBs by looking for task name strings
    fprintf(stderr, "[FreeRTOS] pxCurrentTCB=0x%08X xTickCount=%u\n",
            emu.bus.read32(0x2000F6C0), emu.bus.read32(0x2000F794));
    // Scan SRAM for plausible TCBs: offset 52 has readable ASCII name
    for (uint32_t addr = 0x20000000; addr < 0x20010000; addr += 4) {
        // Check if offset+52 looks like a task name (4+ printable chars starting with uppercase)
        char c0 = emu.bus.read8(addr + 52);
        if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))) continue;
        char name[17] = {};
        bool valid = true;
        int len = 0;
        for (int i = 0; i < 16; i++) {
            char c = emu.bus.read8(addr + 52 + i);
            if (c == 0) break;
            if (c < 32 || c > 126) { valid = false; break; }
            name[i] = c;
            len++;
        }
        if (!valid || len < 3) continue;
        uint32_t sp = emu.bus.read32(addr);
        if (sp < 0x20000000 || sp >= 0x20010000) continue;
        // FreeRTOS M4F context: STMDB {R4-R11, LR(EXC_RETURN)} (9 words), then HW frame (8 words)
        // sp+0..7: R4-R11, sp+8: EXC_RETURN, sp+9..16: R0,R1,R2,R3,R12,LR,PC,xPSR
        uint32_t exc_ret = emu.bus.read32(sp + 8*4);
        uint32_t saved_pc = emu.bus.read32(sp + 15*4);
        uint32_t saved_lr = emu.bus.read32(sp + 14*4);
        uint32_t saved_xpsr = emu.bus.read32(sp + 16*4);
        uint32_t priority = emu.bus.read32(addr + 44);
        fprintf(stderr, "  TCB@0x%08X sp=0x%08X pc=0x%08X exc=0x%08X prio=%u name=\"%s\"\n",
                addr, sp, saved_pc, exc_ret, priority, name);
        // Dump MAI stack for call chain analysis
        if (name[0] == 'M' && name[1] == 'A' && name[2] == 'I') {
            fprintf(stderr, "  [MAI stack dump from SP=0x%08X, 64 words]\n", sp);
            for (int w = 0; w < 64; w++) {
                uint32_t val = emu.bus.read32(sp + w*4);
                if (w % 8 == 0) fprintf(stderr, "    +%03d:", w*4);
                fprintf(stderr, " %08X", val);
                if (w % 8 == 7) fprintf(stderr, "\n");
            }
        }
    }
    // VTOR and runtime vector table content
    uint32_t vtor = emu.bus.read32(0xE000ED08);
    fprintf(stderr, "[NVIC] VTOR=0x%08X\n", vtor);
    fprintf(stderr, "[VTOR] PendSV(14)=0x%08X SysTick(15)=0x%08X\n",
            emu.bus.read32(vtor + 14*4), emu.bus.read32(vtor + 15*4));
    fprintf(stderr, "[VTOR] IRQ0=0x%08X IRQ1=0x%08X IRQ3=0x%08X IRQ17=0x%08X\n",
            emu.bus.read32(vtor + 16*4), emu.bus.read32(vtor + 17*4),
            emu.bus.read32(vtor + 19*4), emu.bus.read32(vtor + 33*4));
    fprintf(stderr, "[VTOR] IRQ11(RTC0)=0x%08X IRQ24(SWI4)=0x%08X IRQ25(SWI5)=0x%08X\n",
            emu.bus.read32(vtor + (16+11)*4), emu.bus.read32(vtor + (16+24)*4), emu.bus.read32(vtor + (16+25)*4));
    // NVIC ISER0 (enable) at 0xE000E100, ISPR0 (pending) at 0xE000E200
    fprintf(stderr, "[NVIC] ISER0=0x%08X ISPR0=0x%08X\n",
            emu.bus.read32(0xE000E100), emu.bus.read32(0xE000E200));
    // IRQ dispatch counts
    fprintf(stderr, "[IRQ dispatch]");
    for (int i = 0; i < 48; i++)
        if (emu.cpu.irq_dispatch_count[i] > 0)
            fprintf(stderr, " %d:%u", i, emu.cpu.irq_dispatch_count[i]);
    fprintf(stderr, "\n");
    // RADIO ISR trampoline target + phy_transition
    fprintf(stderr, "[RADIO ISR] trampoline@0x2000A3F8 → 0x%08X\n",
            emu.bus.read32(0x2000A3F8));
    fprintf(stderr, "[BLE PHY] g_ble_phy_data@0x2000F184: transition=%u state=%u disabled_cb=0x%08X\n",
            emu.bus.read8(0x2000F187), emu.bus.read8(0x2000F186),
            emu.bus.read32(0x2000F1B0));
    // PPI channel dump
    fprintf(stderr, "[PPI] CHEN=0x%08X\n", emu.bus.read32(0x4001F500));
    for (int i = 0; i < 20; i++) {
        uint32_t eep = emu.bus.read32(0x4001F510 + i*8);
        uint32_t tep = emu.bus.read32(0x4001F514 + i*8);
        if (eep || tep) fprintf(stderr, "  CH[%d]: EEP=0x%08X TEP=0x%08X\n", i, eep, tep);
    }
    // Radio state
    fprintf(stderr, "[CLOCK] HFCLKSTARTED=%u HFCLKRUN=%u LFCLKSTARTED=%u\n",
            emu.bus.read32(0x40000100), emu.bus.read32(0x40000104), emu.bus.read32(0x40000108));
    fprintf(stderr, "[Radio] state=%u shorts=0x%08X inten=0x%08X\n",
            emu.bus.read32(0x40001550), emu.bus.read32(0x40001200), emu.bus.read32(0x40001300));
    fprintf(stderr, "[Radio] ev: ready=%u addr=%u payload=%u end=%u disabled=%u\n",
            emu.bus.read32(0x40001100), emu.bus.read32(0x40001104),
            emu.bus.read32(0x40001108), emu.bus.read32(0x4000110C),
            emu.bus.read32(0x40001110));
    // TIMER0 state
    fprintf(stderr, "[TIMER0] mode=%u bitmode=%u prescaler=%u CC[0]=%u CC[1]=%u CC[2]=%u shorts=0x%08X inten=0x%08X\n",
            emu.bus.read32(0x40008504), emu.bus.read32(0x40008508),
            emu.bus.read32(0x40008510),
            emu.bus.read32(0x40008540), emu.bus.read32(0x40008544),
            emu.bus.read32(0x40008548), emu.bus.read32(0x40008200),
            emu.bus.read32(0x40008300));
    fprintf(stderr, "[TIMER0] running=%d counter=%u starts=%u stops=%u fires=%u ev_cmp: [0]=%u [1]=%u [2]=%u [3]=%u\n",
            emu.timer0.is_running(), emu.timer0.counter(),
            emu.timer0.start_count_, emu.timer0.stop_count_, emu.timer0.tick_fires_,
            emu.bus.read32(0x40008140), emu.bus.read32(0x40008144),
            emu.bus.read32(0x40008148), emu.bus.read32(0x4000814C));
    fprintf(stderr, "[RTC0] running=%d counter=%u prescaler=%u inten=0x%08X evten=0x%08X irq_fires=%u CC: %u %u %u %u\n",
            emu.rtc0.diag_running(), emu.rtc0.diag_counter(), emu.rtc0.diag_prescaler(),
            emu.rtc0.diag_inten(), emu.rtc0.diag_evten(), emu.rtc0.irq_fire_count_,
            emu.rtc0.diag_cc(0), emu.rtc0.diag_cc(1), emu.rtc0.diag_cc(2), emu.rtc0.diag_cc(3));
    fprintf(stderr, "[RADIO] txen=%u rxen=%u state=%u inten=0x%08X shorts=0x%X\n",
            emu.radio.txen_count(), emu.radio.rxen_count(),
            emu.radio.get_state(), emu.bus.read32(0x40001300), emu.bus.read32(0x40001200));
    // Read literal at 0x35ba0 (uxTopReadyPriority address) and 0x35ba4 (pxReadyTasksLists address)
    uint32_t addr_topready = emu.bus.read32(0x35ba0);
    uint32_t addr_readylists = emu.bus.read32(0x35ba4);
    fprintf(stderr, "[FreeRTOS] uxTopReadyPriority@0x%08X=0x%08X, pxReadyTasksLists@0x%08X\n",
            addr_topready, emu.bus.read32(addr_topready), addr_readylists);
    // Dump first few ready lists (each 20 bytes: uxNumberOfItems, pxIndex, xListEnd[12])
    for (int p = 0; p < 5; p++) {
        uint32_t list = addr_readylists + p * 20;
        uint32_t items = emu.bus.read32(list);
        if (items > 0)
            fprintf(stderr, "  Ready[%d]: %d items, pxIndex=0x%08X\n", p, items, emu.bus.read32(list+4));
    }
    // BKPT with R0=0 → exit 0 (PASS), R0>0 → exit R0 (test number that failed)
    if (emu.cpu.halted) return emu.cpu.r[0] == 0 ? 0 : (int)emu.cpu.r[0];
    return 124; // timeout
}

static int run_interactive(EmulatorContext& emu) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int scale = 2;
    SDL_Window* window = SDL_CreateWindow("PineTime Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        DISPLAY_W * scale, DISPLAY_H * scale, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_W, DISPLAY_H);

    constexpr uint64_t CYCLES_PER_FRAME = 64'000'000 / 60;
    bool running = true;
    uint64_t last_status = 0;

    while (running && !emu.cpu.halted) {
        uint32_t frame_start = SDL_GetTicks();

        // Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN: {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.key.keysym.sym == SDLK_SPACE)
                    emu.gpio.set_pin(PineDef::BUTTON, true);
                if (!event.key.repeat) {
                    uint8_t gesture = 0;
                    switch (event.key.keysym.sym) {
                    case SDLK_w: gesture = 0x02; break; // Swipe Up
                    case SDLK_s: gesture = 0x01; break; // Swipe Down
                    case SDLK_a: gesture = 0x03; break; // Swipe Left
                    case SDLK_d: gesture = 0x04; break; // Swipe Right
                    default: break;
                    }
                    if (gesture) {
                        const char* names[] = {"?","Down","Up","Left","Right"};
                        fprintf(stderr, "[TOUCH] %s (gesture=%d) at %.1fs\n",
                                names[gesture], gesture, emu.cpu.cycles / 64e6);
                        emu.touch.set_touch(false, 0, 0);
                        emu.touch.set_touch(true, 120, 120, gesture);
                    }
                }
                break;
            }
            case SDL_KEYUP:
                if (event.key.keysym.sym == SDLK_SPACE)
                    emu.gpio.set_pin(PineDef::BUTTON, false);
                if (event.key.keysym.sym == SDLK_w || event.key.keysym.sym == SDLK_a ||
                    event.key.keysym.sym == SDLK_s || event.key.keysym.sym == SDLK_d)
                    emu.touch.set_touch(false, 0, 0);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mx = event.button.x / scale;
                    int my = event.button.y / scale;
                    emu.touch.set_touch(true, mx, my, 0x05);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                emu.touch.set_touch(false, 0, 0);
                break;
            }
        }

        // Run CPU for one frame (event-driven scheduling)
        uint64_t frame_target = emu.cpu.cycles + CYCLES_PER_FRAME;
        while (emu.cpu.cycles < frame_target && !emu.cpu.halted) {
            uint64_t next_ev = emu.next_peripheral_event();
            uint64_t limit = emu.cpu.cycles + 10000;
            uint64_t batch = std::min({next_ev, limit, frame_target});
            if (batch <= emu.cpu.cycles) batch = emu.cpu.cycles + 1;

            emu.cpu.yield_at = next_ev;
            if (emu.cpu.wfi) {
                emu.cpu.cycles = batch;
                emu.tick_peripherals();
                emu.cpu.check_exceptions();
                if (emu.cpu.event_reg) emu.cpu.wfi = false;
            } else {
                emu.cpu.run(batch);
                emu.tick_peripherals();
                emu.cpu.check_exceptions();
            }
            emu.ble_hci.poll(emu.cpu.cycles);
        }

        // Periodic status (every ~1s emulated time)
        if (emu.cpu.cycles - last_status >= 64'000'000) {
            last_status = emu.cpu.cycles;
            char title[128];
            snprintf(title, sizeof(title), "PineTime  %.0fs  HRS: rd=%u wr=%u en=0x%02X",
                     emu.cpu.cycles / 64e6,
                     emu.hrs.read_count_, emu.hrs.write_count_,
                     emu.hrs.reg(0x01));
            SDL_SetWindowTitle(window, title);
            fprintf(stderr, "[DIAG] %.0fs HRS: rd=%u en=0x%02X\n",
                    emu.cpu.cycles / 64e6,
                    emu.hrs.read_count_, emu.hrs.reg(0x01));
        }

        // Render display
        render_display(texture, emu.display);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        uint32_t frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < 16) SDL_Delay(16 - frame_time);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// ============================================================================
// Main
// ============================================================================

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options] <firmware.bin>\n", prog);
    fprintf(stderr, "  --headless          Run without display\n");
    fprintf(stderr, "  --timeout <ms>      Timeout in headless mode (default: 5000)\n");
    fprintf(stderr, "  --ble-hci [port]    BLE HCI over TCP (default port: 9001)\n");
    fprintf(stderr, "  --companion         Auto-sync time, music, notifications, weather\n");
}

int main(int argc, char* argv[]) {
    const char* firmware_path = nullptr;
    bool headless = false;
    bool trace = false;
    uint32_t timeout_ms = 5000;
    int ble_hci_port = 0; // 0 = disabled
    bool companion = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) headless = true;
        else if (strcmp(argv[i], "--trace") == 0) trace = true;
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) timeout_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ble-hci") == 0) {
            ble_hci_port = 9001;
            if (i + 1 < argc && argv[i+1][0] >= '0' && argv[i+1][0] <= '9') ble_hci_port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--companion") == 0) companion = true;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        else if (argv[i][0] != '-') firmware_path = argv[i];
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); return 1; }
    }

    if (!firmware_path) { usage(argv[0]); return 1; }

    EmulatorContext emu;
    if (!emu.flash.load_file(firmware_path)) {
        fprintf(stderr, "Failed to load firmware: %s\n", firmware_path);
        return 1;
    }

    fprintf(stderr, "[EMU] Loaded %s\n", firmware_path);

    emu.cpu.reset();
    fprintf(stderr, "[EMU] Reset: SP=0x%08X PC=0x%08X\n", emu.cpu.r[13], emu.cpu.r[15]);

    if (ble_hci_port > 0) {
        if (!emu.ble_hci.start(ble_hci_port)) {
            fprintf(stderr, "[BLE-HCI] Failed to start on port %d\n", ble_hci_port);
            return 1;
        }
    }

    if (companion) {
        emu.ble_hci.set_companion_mode(true);
        fprintf(stderr, "[EMU] Companion mode: will auto-sync time, music, notifications, weather\n");
    }

    if (headless)
        return run_headless(emu, timeout_ms, trace);
    else
        return run_interactive(emu);
}
