#include "bus.hpp"
#include "cpu.hpp"
#include "device/memory.hpp"
#include "device/nvic.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// nRF52832 / PineTime memory map
static constexpr uint32_t FLASH_BASE = 0x00000000;
static constexpr uint32_t FLASH_SIZE = 512 * 1024;
static constexpr uint32_t SRAM_BASE  = 0x20000000;
static constexpr uint32_t SRAM_SIZE  = 64 * 1024;
static constexpr uint32_t SCS_BASE   = 0xE000E000; // System Control Space
static constexpr uint32_t SCS_SIZE   = 0x1000;

// PineTime display
static constexpr int DISPLAY_W = 240;
static constexpr int DISPLAY_H = 240;

// nRF52832 peripheral base addresses (stub region)
static constexpr uint32_t PERIPH_BASE = 0x40000000;
static constexpr uint32_t PERIPH_SIZE = 0x20000000; // 0x40000000-0x5FFFFFFF

// FICR / UICR
static constexpr uint32_t FICR_BASE = 0x10000000;
static constexpr uint32_t FICR_SIZE = 0x1000;
static constexpr uint32_t UICR_BASE = 0x10001000;
static constexpr uint32_t UICR_SIZE = 0x1000;

// Stub device: returns 0 on read, ignores write (silently)
struct StubDevice : pt::Device {
    uint32_t read(uint32_t /*addr*/, pt::Width /*w*/) override { return 0; }
    void write(uint32_t /*addr*/, pt::Width /*w*/, uint32_t /*val*/) override {}
};

// FICR: Factory Information Configuration Registers
// Returns plausible values for nRF52832
struct FICR : pt::Device {
    uint32_t read(uint32_t addr, pt::Width /*w*/) override {
        switch (addr) {
        case 0x010: return 0xFFFFFFFF;  // CODEPAGESIZE (not present → default)
        case 0x014: return 128;         // CODESIZE (128 pages × 4KB = 512KB)
        case 0x060: return 0x00000034;  // CONFIGID
        case 0x064: return 0x00052832;  // DEVICEID[0]
        case 0x068: return 0x12345678;  // DEVICEID[1]
        case 0x0A0: return 0x00000006;  // INFO.PART = nRF52832
        case 0x0A4: return 0x00000002;  // INFO.VARIANT = QFAA
        case 0x0A8: return 0x00010000;  // INFO.PACKAGE
        case 0x0AC: return 0x00040000;  // INFO.RAM = 64KB
        case 0x0B0: return 0x00080000;  // INFO.FLASH = 512KB
        }
        return 0xFFFFFFFF;
    }
    void write(uint32_t, pt::Width, uint32_t) override {}
};

struct EmulatorContext {
    pt::Flash flash{FLASH_SIZE};
    pt::RAM sram{SRAM_SIZE};
    pt::NVIC nvic;
    FICR ficr;
    StubDevice uicr_stub;
    StubDevice periph_stub;

    pt::Bus bus;
    pt::CPU cpu{&bus};

    EmulatorContext() {
        bus.map(FLASH_BASE, FLASH_SIZE, &flash);
        bus.map(SRAM_BASE, SRAM_SIZE, &sram);
        bus.map(SCS_BASE, SCS_SIZE, &nvic);
        bus.map(FICR_BASE, FICR_SIZE, &ficr);
        bus.map(UICR_BASE, UICR_SIZE, &uicr_stub);
        bus.map(PERIPH_BASE, PERIPH_SIZE, &periph_stub);

        bus.set_fast_path(flash.data(), FLASH_SIZE,
                          sram.data(), SRAM_BASE, SRAM_SIZE);
        cpu.set_nvic(&nvic);
    }

    void tick_peripherals() {
        nvic.tick(cpu.cycles);
    }
};

// ============================================================================
// Run modes
// ============================================================================

static int run_headless(EmulatorContext& emu, uint32_t timeout_ms) {
    uint64_t max_cycles = (uint64_t)timeout_ms * 64000; // ~64MHz
    uint64_t batch = 10000;
    uint64_t start = emu.cpu.cycles;

    while (emu.cpu.cycles - start < max_cycles && !emu.cpu.halted) {
        uint64_t target = emu.cpu.cycles + batch;
        emu.cpu.run(target);
        emu.tick_peripherals();
        emu.cpu.check_exceptions();
    }

    uint64_t elapsed = emu.cpu.cycles - start;
    double mips = elapsed / 1e6;
    fprintf(stderr, "[EMU] %llu cycles (%.2f M), %s\n",
            (unsigned long long)elapsed, mips,
            emu.cpu.halted ? "HALTED" : "TIMEOUT");
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

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
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

        // TODO: Render display framebuffer when ST7789 device is implemented
        // For now: clear to dark gray
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
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
    fprintf(stderr, "  --trace             Print executed instructions to stderr\n");
}

int main(int argc, char* argv[]) {
    const char* firmware_path = nullptr;
    bool headless = false;
    uint32_t timeout_ms = 5000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) headless = true;
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
        return run_headless(emu, timeout_ms);
    else
        return run_interactive(emu);
}
