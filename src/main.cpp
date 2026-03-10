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
#include "device/radio.hpp"
#include "device/ppi.hpp"
#include "device/saadc.hpp"
#include "device/temp.hpp"
#include <SDL2/SDL.h>
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
    StubDevice fpu_stub;
    StubDevice ccm_stub;
    StubDevice swi0_stub, swi1_stub, swi2_stub, swi3_stub, swi4_stub, swi5_stub;
    StubDevice stub_device;
    DWT dwt;

    // PineTime devices
    pt::ST7789 display;
    pt::CST816S touch;
    pt::HRS3300 hrs;
    pt::BMA4xx bma;

    // External SPI flash (4MB, all 0xFF = empty)
    // Also memory-mapped at 0x04000000 for MCUBoot/InfiniTime flash area access
    static constexpr uint32_t EXT_FLASH_SIZE = 4 * 1024 * 1024;
    static constexpr uint32_t EXT_FLASH_MMAP = 0x04000000;
    std::vector<uint8_t> ext_flash = std::vector<uint8_t>(EXT_FLASH_SIZE, 0xFF);

    // Memory-mapped view of external SPI flash
    struct ExtFlashMmap : pt::Device {
        std::vector<uint8_t>* flash;
        uint32_t read(uint32_t addr, pt::Width w) override {
            if (addr >= flash->size()) return 0xFF;
            switch (w) {
            case pt::Width::Byte: return (*flash)[addr];
            case pt::Width::Half: return (*flash)[addr] | ((*flash)[addr+1] << 8);
            case pt::Width::Word: return (*flash)[addr] | ((*flash)[addr+1] << 8) |
                                         ((*flash)[addr+2] << 16) | ((*flash)[addr+3] << 24);
            }
            return 0xFF;
        }
        void write(uint32_t addr, pt::Width w, uint32_t val) override {
            if (addr >= flash->size()) return;
            switch (w) {
            case pt::Width::Byte: (*flash)[addr] = val; break;
            case pt::Width::Half: (*flash)[addr] = val; (*flash)[addr+1] = val >> 8; break;
            case pt::Width::Word:
                (*flash)[addr] = val; (*flash)[addr+1] = val >> 8;
                (*flash)[addr+2] = val >> 16; (*flash)[addr+3] = val >> 24; break;
            }
        }
    } ext_flash_mmap;

    // SPI flash state machine (for byte-at-a-time legacy SPI)
    uint8_t flash_cmd_ = 0;
    uint32_t flash_addr_ = 0;
    uint8_t flash_phase_ = 0;   // 0=cmd, 1-3=addr bytes, 4+=data
    bool flash_wren_ = false;

    void spi_flash_reset_cmd() { flash_cmd_ = 0; flash_phase_ = 0; }

    void spi_flash_transfer(const uint8_t* tx, uint32_t tx_len,
                            uint8_t* rx, uint32_t rx_len) {
        // DMA transfers: process whole buffer at once
        if (tx_len > 1) {
            spi_flash_dma(tx, tx_len, rx, rx_len);
            return;
        }
        // Legacy SPI: byte-at-a-time
        if (tx_len == 0) return;
        uint8_t tx_byte = tx[0];
        uint8_t rx_byte = 0xFF;

        if (flash_phase_ == 0) {
            // Command byte
            flash_cmd_ = tx_byte;
            flash_addr_ = 0;
            flash_phase_ = 1;
            switch (flash_cmd_) {
            case 0x06: flash_wren_ = true; flash_phase_ = 0; break;
            case 0x04: flash_wren_ = false; flash_phase_ = 0; break;
            case 0xC7: // CHIP_ERASE
                if (flash_wren_) memset(ext_flash.data(), 0xFF, ext_flash.size());
                flash_wren_ = false; flash_phase_ = 0; break;
            case 0xAB: flash_phase_ = 4; break; // skip 3 dummy bytes then ID
            }
        } else if (flash_cmd_ == 0x9F) {
            // JEDEC ID: XT25F32B (XTX, matches PineTime hardware)
            static const uint8_t jedec[] = {0x0B, 0x40, 0x16};
            rx_byte = (flash_phase_ <= 3) ? jedec[flash_phase_ - 1] : 0xFF;
            flash_phase_++;
        } else if (flash_cmd_ == 0x05) {
            rx_byte = 0; // Status: not busy
            flash_phase_++;
        } else if (flash_phase_ <= 3) {
            // Address bytes (3 bytes, MSB first)
            flash_addr_ = (flash_addr_ << 8) | tx_byte;
            flash_phase_++;
        } else if (flash_cmd_ == 0x0B && flash_phase_ == 4) {
            // FAST_READ dummy byte
            flash_phase_++;
        } else {
            // Data phase
            uint32_t fa = flash_addr_;
            switch (flash_cmd_) {
            case 0x03: // READ
            case 0x0B: // FAST_READ
                rx_byte = (fa < ext_flash.size()) ? ext_flash[fa] : 0xFF;
                flash_addr_++;
                break;
            case 0x02: // PAGE_PROGRAM
                if (fa < ext_flash.size() && flash_wren_) ext_flash[fa] &= tx_byte;
                flash_addr_++;
                break;
            case 0x20: // SECTOR_ERASE (addr already collected)
            case 0xD8: { // BLOCK_ERASE
                if (flash_wren_) {
                    uint32_t len = (flash_cmd_ == 0x20) ? 4096 : 65536;
                    uint32_t base = fa & ~(len - 1);
                    if (base + len <= ext_flash.size())
                        memset(&ext_flash[base], 0xFF, len);
                }
                flash_wren_ = false;
                flash_phase_ = 0;
                break;
            }
            case 0xAB: // Release Deep Power-Down + Read ID
                rx_byte = 0x15; // Device ID
                flash_phase_++;
                break;
            default:
                break;
            }
        }

        if (rx_len > 0) rx[0] = rx_byte;
    }

    void spi_flash_dma(const uint8_t* tx, uint32_t tx_len,
                       uint8_t* rx, uint32_t rx_len) {
        if (tx_len == 0) return;
        uint8_t cmd = tx[0];
        switch (cmd) {
        case 0x9F:
            if (rx_len > 1) rx[1] = 0x0B;  // XTX
            if (rx_len > 2) rx[2] = 0x40;
            if (rx_len > 3) rx[3] = 0x16;
            break;
        case 0x05:
            if (rx_len > 1) rx[1] = 0;
            break;
        case 0x03:
        case 0x0B: {
            if (tx_len < 4) break;
            uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
            uint32_t skip = (cmd == 0x0B) ? 5 : 4;
            for (uint32_t i = skip; i < rx_len; i++) {
                uint32_t fa = addr + (i - skip);
                rx[i] = (fa < ext_flash.size()) ? ext_flash[fa] : 0xFF;
            }
            break;
        }
        case 0x06: flash_wren_ = true; break;
        case 0x04: flash_wren_ = false; break;
        case 0x02: {
            if (tx_len < 5 || !flash_wren_) break;
            uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
            for (uint32_t i = 4; i < tx_len; i++) {
                uint32_t fa = addr + (i - 4);
                if (fa < ext_flash.size()) ext_flash[fa] &= tx[i];
            }
            flash_wren_ = false;
            break;
        }
        case 0x20:
        case 0xD8: {
            if (tx_len < 4 || !flash_wren_) break;
            uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
            uint32_t len = (cmd == 0x20) ? 4096 : 65536;
            addr &= ~(len - 1);
            if (addr + len <= ext_flash.size())
                memset(&ext_flash[addr], 0xFF, len);
            flash_wren_ = false;
            break;
        }
        case 0xC7:
            if (flash_wren_) memset(ext_flash.data(), 0xFF, ext_flash.size());
            flash_wren_ = false;
            break;
        }
        flash_phase_ = 0;
    }

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
        saadc.set_bus(&bus);
        saadc.set_nvic(&nvic, 7);
        temp.set_nvic(&nvic, 12);
        rng.set_nvic(&nvic, 13);
        bus.map(0x4000E000, 0x1000, &ecb);         // ECB
        ecb.set_bus(&bus);
        bus.map(0x4000F000, 0x1000, &ccm_stub);    // CCM/AAR
        bus.map(0x40010000, 0x1000, &wdt);         // WDT
        bus.map(0x40011000, 0x1000, &rtc1);        // RTC1
        bus.map(0x4001E000, 0x1000, &nvmc);        // NVMC
        bus.map(0x40014000, 0x1000, &swi0_stub);    // SWI0/EGU0
        bus.map(0x40015000, 0x1000, &swi1_stub);    // SWI1/EGU1
        bus.map(0x40016000, 0x1000, &swi2_stub);    // SWI2/EGU2
        bus.map(0x40017000, 0x1000, &swi3_stub);    // SWI3/EGU3
        bus.map(0x40018000, 0x1000, &swi4_stub);    // SWI4/EGU4
        bus.map(0x40019000, 0x1000, &swi5_stub);    // SWI5/EGU5
        bus.map(0x4001F000, 0x1000, &ppi);            // PPI (at BPROT addr for nRF52)
        bus.map(0x40020000, 0x1000, &stub_device);  // Reserved
        bus.map(0x40024000, 0x1000, &rtc2);        // RTC2
        bus.map(0x4002A000, 0x1000, &fpu_stub);    // FPU access port
        bus.map(0x50000000, 0x1000, &gpio);         // GPIO P0
        ext_flash_mmap.flash = &ext_flash;
        bus.map(EXT_FLASH_MMAP, EXT_FLASH_SIZE, &ext_flash_mmap); // External SPI flash memory-mapped
        bus.map(0xE0001000, 0x1000, &dwt);           // DWT (Data Watchpoint and Trace)
        bus.map(0xF0000000, 0x1000, &stub_device);   // CoreSight ROM Table

        bus.set_fast_path(flash.data(), FLASH_SIZE,
                          sram.data(), SRAM_BASE, SRAM_SIZE);

        // Wire CPU to NVIC and DWT
        cpu.set_nvic(&nvic);
        nvic.set_irq_pending_flag(&cpu.irq_pending);
        dwt.set_cpu(&cpu);

        // Wire peripherals to NVIC (nRF52 IRQ numbers)
        clock_power.set_nvic(&nvic, 0);
        timer0.set_nvic(&nvic, 8);
        timer1.set_nvic(&nvic, 9);
        timer2.set_nvic(&nvic, 10);
        rtc0.set_nvic(&nvic, 11);
        rtc1.set_nvic(&nvic, 17);
        rtc2.set_nvic(&nvic, 36);

        // Wire SPIM/TWIM to bus (for DMA) and NVIC
        spim0.set_bus(&bus);
        spim0.set_nvic(&nvic, 3);
        twim1.set_bus(&bus);
        twim1.set_nvic(&nvic, 4);

        // Wire NVMC to flash
        nvmc.set_flash(&flash);
        flash.set_nvmc_config(nvmc.config_ptr());

        // Connect PineTime display to SPIM0
        spim0.set_callback([this](const uint8_t* tx, uint32_t tx_len,
                                   uint8_t* rx, uint32_t rx_len) {
            bool lcd_cs = gpio.get_pin(PineDef::LCD_CS);
            bool flash_cs = gpio.get_pin(PineDef::SPI_FLASH_CS);
            if (!flash_cs) {
                spi_flash_transfer(tx, tx_len, rx, rx_len);
            }
            if (!lcd_cs) {
                // LCD selected
                bool dc = gpio.get_pin(PineDef::LCD_DC);
                display.set_dc(dc);
                display.spi_transfer(tx, tx_len);
            }
        });

        // Connect I2C devices to TWIM1
        twim1.attach(pt::CST816S::I2C_ADDR, &touch);
        twim1.attach(pt::HRS3300::I2C_ADDR, &hrs);
        twim1.attach(pt::BMA4xx::I2C_ADDR, &bma);

        // Set PineTime default input pin states
        gpio.set_pin(PineDef::CHARGE_IND, true);  // Not charging
        gpio.set_pin(PineDef::POWER_PRES, false);  // No USB power
        gpio.set_pin(PineDef::BUTTON, false);       // Button not pressed

        // Reset SPI flash state when CS pin goes high (deselected)
        gpio.set_on_out_change([this](uint32_t old_out, uint32_t new_out) {
            uint32_t rising = new_out & ~old_out; // pins that went high
            if (rising & (1u << PineDef::SPI_FLASH_CS))
                spi_flash_reset_cmd();
        });
    }

    void tick_peripherals() {
        nvic.tick(cpu.cycles);
        timer0.tick(cpu.cycles);
        timer1.tick(cpu.cycles);
        timer2.tick(cpu.cycles);
        rtc0.tick(cpu.cycles);
        rtc1.tick(cpu.cycles);
        rtc2.tick(cpu.cycles);
        rng.tick(cpu.cycles);
        temp.tick(cpu.cycles);
    }
};

// ============================================================================
// Display rendering: RGB565 → RGB888
// ============================================================================

static void render_display(SDL_Texture* tex, const pt::ST7789& display) {
    uint32_t* pixels;
    int pitch;
    SDL_LockTexture(tex, nullptr, (void**)&pixels, &pitch);

    const uint16_t* fb = display.framebuffer();
    bool on = display.is_on();

    for (int y = 0; y < DISPLAY_H; y++) {
        for (int x = 0; x < DISPLAY_W; x++) {
            uint32_t color = 0;
            if (on) {
                uint16_t c = fb[y * DISPLAY_W + x];
                uint8_t r = ((c >> 11) & 0x1F) << 3;
                uint8_t g = ((c >> 5) & 0x3F) << 2;
                uint8_t b = (c & 0x1F) << 3;
                color = (r << 16) | (g << 8) | b;
            }
            pixels[y * (pitch / 4) + x] = color;
        }
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

    while (emu.cpu.cycles - start < max_cycles && !emu.cpu.halted) {
        uint64_t target = emu.cpu.cycles + 10000;
        emu.cpu.run(target);
        emu.tick_peripherals();
        emu.cpu.check_exceptions();

        if (trace && emu.cpu.cycles - last_report >= 1000000) {
            fprintf(stderr, "[TRACE] %8.2fM  PC=0x%08X  SP=0x%08X  LR=0x%08X  xPSR=0x%08X\n",
                    (emu.cpu.cycles - start) / 1e6,
                    emu.cpu.r[15], emu.cpu.r[13], emu.cpu.r[14], emu.cpu.xpsr);
            last_report = emu.cpu.cycles;
        }
    }

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
    // Display state
    {
        const uint16_t* fb = emu.display.framebuffer();
        int nonzero = 0;
        for (int i = 0; i < 240*240; i++) if (fb[i]) nonzero++;
        fprintf(stderr, "[Display] on=%d, non-zero pixels: %d/%d\n",
                emu.display.is_on() ? 1 : 0, nonzero, 240*240);
    }
    // FreeRTOS state dump
    fprintf(stderr, "[FreeRTOS] pxCurrentTCB=0x%08X uxSchedulerSuspended=%d xTickCount=%u xNextUnblock=%u\n",
            emu.bus.read32(0x2000F6C0), emu.bus.read32(0x2000F714),
            emu.bus.read32(0x2000F794), emu.bus.read32(0x2000F74C));
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
    return emu.cpu.halted ? 1 : 0;
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
        SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_W, DISPLAY_H);

    constexpr uint64_t CYCLES_PER_FRAME = 64'000'000 / 60;
    bool running = true;

    while (running && !emu.cpu.halted) {
        uint32_t frame_start = SDL_GetTicks();

        // Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.key.keysym.sym == SDLK_SPACE)
                    emu.gpio.set_pin(PineDef::BUTTON, true); // Button press
                break;
            case SDL_KEYUP:
                if (event.key.keysym.sym == SDLK_SPACE)
                    emu.gpio.set_pin(PineDef::BUTTON, false);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mx = event.button.x / scale;
                    int my = event.button.y / scale;
                    emu.touch.set_touch(true, mx, my);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                emu.touch.set_touch(false, 0, 0);
                break;
            case SDL_MOUSEMOTION:
                if (event.motion.state & SDL_BUTTON_LMASK) {
                    int mx = event.motion.x / scale;
                    int my = event.motion.y / scale;
                    emu.touch.set_touch(true, mx, my);
                }
                break;
            }
        }

        // Run CPU for one frame
        uint64_t target = emu.cpu.cycles + CYCLES_PER_FRAME;
        while (emu.cpu.cycles < target && !emu.cpu.halted && !emu.cpu.wfi) {
            uint64_t batch = std::min<uint64_t>(emu.cpu.cycles + 10000, target);
            emu.cpu.run(batch);
            emu.tick_peripherals();
            emu.cpu.check_exceptions();
        }

        if (emu.cpu.wfi) {
            emu.tick_peripherals();
            emu.cpu.check_exceptions();
        }

        // Render display
        render_display(texture, emu.display);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        uint32_t frame_time = SDL_GetTicks() - frame_start;
        uint32_t target_ms = emu.cpu.wfi ? 100 : 16;
        if (frame_time < target_ms) SDL_Delay(target_ms - frame_time);
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
}

int main(int argc, char* argv[]) {
    const char* firmware_path = nullptr;
    bool headless = false;
    bool trace = false;
    uint32_t timeout_ms = 5000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) headless = true;
        else if (strcmp(argv[i], "--trace") == 0) trace = true;
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) timeout_ms = atoi(argv[++i]);
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

    if (headless)
        return run_headless(emu, timeout_ms, trace);
    else
        return run_interactive(emu);
}
