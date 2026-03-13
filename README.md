# PineTime Emulator

nRF52832 / ARM Cortex-M4F emulator for the [PineTime](https://wiki.pine64.org/index.php/PineTime) smartwatch. Boots [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime) without hardware.

## Features

- **Thumb-2 CPU** with VFPv4-D16 FPU (single+double precision), IT blocks, FPU lazy stacking, LDREX/STREX exclusive monitor
- **Full nRF52832 peripherals**: NVIC, SysTick, GPIO/GPIOTE, TIMER, RTC, SPIM, TWIM, UARTE, RADIO, PPI, SAADC, TEMP, RNG, ECB, NVMC, WDT, EGU, CLOCK/POWER
- **PineTime hardware**: ST7789 display (SDL2), CST816S touch, HRS3300 heart rate, BMA421 accelerometer, 4MB SPI NOR flash
- **BLE**: RADIO emulation with HCI-over-TCP bridge or built-in virtual companion
- **Performance**: ~3x real-time (decode cache, block chaining, direct flash access)
- **37 test suites** (25 C++ unit tests + 12 ARM assembly tests)

## Target Hardware

PineTime: nRF52832 (Cortex-M4F @ 64MHz), 64KB SRAM, 512KB flash + 4MB ext. SPI NOR.
Display ST7789 240x240 SPI, touch CST816S I2C, HR HRS3300 I2C, accel BMA421 I2C.

## Build

Dependencies: `cmake ninja-build gcc-arm-none-eabi libsdl2-dev`

```bash
cmake -B build -G Ninja . && ninja -C build   # Emulator + tests
ctest --test-dir build                         # All 37 test suites
```

## Usage

```bash
./build/pinetime-emu firmware.bin                     # Interactive (SDL2 window)
./build/pinetime-emu --headless --timeout 5000 fw.bin # Headless
./build/pinetime-emu --ble-hci firmware.bin           # BLE HCI over TCP (port 9001)
./build/pinetime-emu --companion firmware.bin         # Virtual companion (auto-sync)
```

### Input

| Key / Action | Function |
|---|---|
| W / A / S / D | Swipe up / left / down / right |
| Mouse click | Tap |
| Space | Side button |

### BLE Modes

**`--ble-hci`**: TCP server on port 9001 with HCI H4 framing. Connect external host stacks (Bumble, BlueZ vhci, Gadgetbridge).

**`--companion`**: Built-in BLE companion, no external host needed. Automatically syncs time (CTS), music metadata, and notifications (ANS). Communicates directly with NimBLE via RADIO emulation.

## Architecture

```
┌──────────────────────────────────────────────┐
│ EmulatorContext                               │
│  ┌─────┐  ┌─────────────────────────────────┐│
│  │ CPU │──│ Bus                              ││
│  └─────┘  │  Fast path: Flash/SRAM (pointer) ││
│           │  Slow path: Peripherals (vtable) ││
│           └─────────────────────────────────┘│
│  ┌────────────────────────────────────────┐  │
│  │ Peripherals (Device interface)         │  │
│  │  read(addr) / write(addr, val) / tick()│  │
│  │  next_event() → event-driven scheduling│  │
│  └────────────────────────────────────────┘  │
│  ┌──────────┐  ┌───────────┐                 │
│  │ ST7789   │──│ SDL2      │                 │
│  │ SPI→FB   │  │ Rendering │                 │
│  └──────────┘  └───────────┘                 │
└──────────────────────────────────────────────┘
```

- **Device interface**: `read(addr, Width)`, `write(addr, Width, val)`, `tick(cycles)`, `next_event(now)`
- **Event scheduler**: CPU runs until the next peripheral event — no fixed tick grid
- **Interrupts**: Peripherals set pending via NVIC, CPU checks in `check_exceptions()`

## Peripherals

| Address | Peripheral | Description |
|---------|-----------|-------------|
| 0x00000000 | Flash | 512KB (read-only) |
| 0x20000000 | SRAM | 64KB |
| 0x40000000 | CLOCK + POWER | Clock sources, power management |
| 0x40001000 | RADIO | BLE radio state machine |
| 0x40002000 | UARTE0 | UART with EasyDMA |
| 0x40003000 | SPIM0 | SPI master → ST7789 display |
| 0x40004000 | TWIM1 | I2C master → touch/HR/accel |
| 0x40006000 | GPIOTE | GPIO tasks and events |
| 0x40008000–0x4000A000 | TIMER0–2 | 32-bit timer (6 CC) |
| 0x4000B000, 0x40011000, 0x40024000 | RTC0–2 | 24-bit RTC (4 CC) |
| 0x4000C000 | TEMP | Die temperature |
| 0x4000D000 | RNG | Hardware random number generator |
| 0x4000E000 | ECB | AES-128 encryption |
| 0x40010000 | WDT | Watchdog timer |
| 0x40014000–0x40019000 | EGU0–5 | Software interrupts |
| 0x4001E000 | NVMC | Flash controller |
| 0x4001F000 | PPI | Programmable peripheral interconnect |
| 0x50000000 | GPIO P0 | 32 pins |
| 0xE000E000 | NVIC + SCB + SysTick | System control space |

## Project Structure

```
src/
├── main.cpp           # EmulatorContext, SDL2 main loop
├── cpu.hpp/cpp        # Cortex-M4F Thumb-2 execution engine
├── bus.hpp            # Device interface, bus with fast/slow path
└── device/            # All peripherals (one header per device)
tests/
├── arm/               # ARM assembly tests (cross-compiled, run inside emulator)
└── *.cpp              # C++ unit tests
```

## Status

InfiniTime 1.14.1 boots fully: MCUBoot → InfiniTime handoff, display rendering, touch input, BLE connection with virtual companion. FreeRTOS scheduler stable.
