# PineTime Emulator

nRF52832 / ARM Cortex-M4F Emulator für PineTime Smartwatch. Ziel: InfiniTime ohne Hardware booten.

## Hardware-Ziel

PineTime: nRF52832 (Cortex-M4F @ 64MHz), 64KB SRAM, 512KB Flash + 4MB ext. SPI NOR.
Display ST7789 240×240 SPI, Touch CST816S I2C, HR HRS3300 I2C, Accel BMA421 I2C.

## Projektstruktur

```
pinetime-emu/
├── src/
│   ├── main.cpp              # EmulatorContext, alle Peripherals verdrahtet, SDL2 Main-Loop
│   ├── cpu.hpp               # Cortex-M4F State + Interface
│   ├── cpu.cpp               # Thumb-2 Execution Engine (~1500 Zeilen)
│   ├── bus.hpp               # Device Interface, Bus mit Fast-Path
│   └── device/
│       ├── memory.hpp        # Flash (read-only) + RAM
│       ├── nvic.hpp          # NVIC + SCB + SysTick (System Control Space)
│       ├── clock_power.hpp   # CLOCK + POWER (shared base 0x40000000)
│       ├── gpio.hpp          # GPIO P0 + GPIOTE
│       ├── timer.hpp         # TIMER (32-bit, 6 CC) + RTC (24-bit, 4 CC)
│       ├── spim.hpp          # SPIM mit EasyDMA + SpiCallback
│       ├── twim.hpp          # TWIM mit EasyDMA + I2CDevice-Interface
│       ├── nvmc.hpp          # Flash Controller (CONFIG/ERASEPAGE)
│       ├── radio.hpp         # BLE Radio (TX instant, RX stub)
│       ├── ppi.hpp           # PPI (Programmable Peripheral Interconnect)
│       ├── saadc.hpp         # SAADC (ADC, simuliert Batteriespannung)
│       ├── temp.hpp          # TEMP (Die-Temperatur, 21°C)
│       ├── rng.hpp           # Random Number Generator (VALRDY events)
│       ├── ecb.hpp           # AES-128 ECB Encryption
│       ├── wdt.hpp           # Watchdog (akzeptiert Reload, kein Timeout)
│       ├── st7789.hpp        # ST7789 LCD Controller (240×240 RGB565 Framebuffer)
│       └── i2c_devices.hpp   # CST816S Touch, HRS3300 HR, BMA4xx Accel
├── tests/
│   ├── test_basic.s          # CPU-Grundtest (10 Tests, BKPT-terminiert)
│   ├── test.ld               # Linker-Script für Tests
│   └── Makefile
└── CMakeLists.txt
```

## Build

```bash
cmake -B build -G Ninja . && ninja -C build          # Emulator
make -C tests                                         # ARM-Tests
```

## Ausführen

```bash
./build/pinetime-emu firmware.bin                     # Interaktiv (SDL2)
./build/pinetime-emu --headless --timeout 5000 fw.bin # Headless
```

## Build-Umgebung

Linux (Debian). Pakete: `cmake ninja-build gcc-arm-none-eabi libsdl2-dev`

## Architektur (COSMO-32-Muster)

- **EmulatorContext** zentralisiert Device-Setup (keine globalen Objekte)
- **Bus**: Fast-Path für Flash/SRAM (direkte Pointer), Slow-Path für Peripherals (virtuelle Calls)
- **Device-Interface**: `read(addr, Width)`, `write(addr, Width, val)`, `tick(cycles)`
- **CPU**: `step()` für Einzelschritt, `run(target)` für Batch-Execution
- **Interrupts**: Peripherals setzen pending via NVIC, CPU prüft in `check_exceptions()`

## CPU-Implementierung

Thumb-2 ISA, organisiert in Gruppen:

| Gruppe | Methode | Instruktionen |
|--------|---------|---------------|
| 16-bit Kern | `exec_thumb16()` | MOV, ADD, SUB, CMP, Shifts, Data Processing, LDR/STR, B, BX |
| 16-bit Misc | `exec_thumb16_misc()` | PUSH, POP, CPSIE/D, Extend, REV, CBZ/CBNZ, IT, WFI |
| 32-bit op1=01 | `exec_thumb32_01()` | LDM/STM, LDRD/STRD, LDREX/STREX, TBB/TBH, DP shifted reg |
| 32-bit op1=10 | `exec_thumb32_10()` | DP modified imm, DP plain imm (MOVW/MOVT), Branches (B.W/BL), MSR/MRS |
| 32-bit op1=11 | `exec_thumb32_11()` | LDR.W/STR.W, CLZ/REV/RBIT, MUL/DIV, SMULL/UMULL |
| VFP Data Proc | `exec_vfp()` / `exec_vfp_dp()` | VMOV, VADD, VSUB, VMUL, VDIV, VCMP, VCVT, VABS, VNEG, VSQRT |
| VFP Load/Store | `exec_vfp_ldst()` | VLDR, VSTR, VLDM, VSTM |

**PC-Konvention**: `r[15]` = Adresse der aktuellen Instruktion. Lesen als Operand gibt `r[15] + 4`.
**IT-Blöcke**: Condition-Check vor Execution, `advance_it()` danach. 16-bit Instruktionen setzen keine Flags in IT.
**Exceptions**: Cortex-M Stacking (8 Register), EXC_RETURN Magic Values, NVIC-Prioritäten.

## nRF52832 Peripherals (implementiert)

| Adresse | Gerät | Datei |
|---------|-------|-------|
| 0x00000000 | Flash 512KB | memory.hpp |
| 0x10000000 | FICR (nRF52832 IDs) | main.cpp (RAM-Stub) |
| 0x10001000 | UICR | main.cpp (RAM-Stub) |
| 0x20000000 | SRAM 64KB | memory.hpp |
| 0x40000000 | CLOCK + POWER | clock_power.hpp |
| 0x40003000 | SPIM0 → ST7789 | spim.hpp |
| 0x40004000 | TWIM1 → Touch/HR/Accel | twim.hpp |
| 0x40006000 | GPIOTE | gpio.hpp |
| 0x40008000 | TIMER0 | timer.hpp |
| 0x40009000 | TIMER1 | timer.hpp |
| 0x4000A000 | TIMER2 | timer.hpp |
| 0x40010000 | WDT | wdt.hpp |
| 0x40011000 | RTC0 | timer.hpp |
| 0x40014000 | RTC1 | timer.hpp |
| 0x40024000 | RTC2 | timer.hpp |
| 0x4001E000 | NVMC | nvmc.hpp |
| 0x50000000 | GPIO P0 | gpio.hpp |
| 0xE000E000 | SCS (NVIC+SCB+SysTick) | nvic.hpp |
| 0x40001000 | RADIO (BLE) | radio.hpp |
| 0x40007000 | SAADC (ADC) | saadc.hpp |
| 0x4000C000 | TEMP (Die-Temperatur) | temp.hpp |
| 0x4000D000 | RNG | rng.hpp |
| 0x4000E000 | ECB (AES-128) | ecb.hpp |
| 0x4001F000 | PPI | ppi.hpp |
| Stubs | FPU access port, CCM/AAR | main.cpp |

## Test-Konvention

Tests terminieren via BKPT. R0=0 = PASS, R0=N = Test N fehlgeschlagen.

## Status

**CPU**: Thumb-2 Core + VFPv4-D16 FPU (single+double precision). IT-Blöcke, Exceptions, interrupt-driven scheduling. 10 Basistests bestehen.
**Peripherals**: Alle nRF52-Peripherals implementiert: Radio, PPI, SAADC, TEMP, RNG, ECB, NVMC, SysTick, NVIC, SPIM, TWIM, Timers, RTCs, GPIO/GPIOTE, WDT, CLOCK/POWER.
**PineTime**: ST7789 (SPI→Framebuffer→SDL2), CST816S (Touch→Maus), HRS3300, BMA4xx. Ext. SPI Flash 4MB.
**Firmware**: `firmware/merged-internal.bin` — InfiniTime 1.14.1 + MCUBoot Bootloader.

**InfiniTime Boot**: ✓ Erfolgreich. MCUBoot → InfiniTime Handoff, Display ON, Digital Watchface gerendert (8k/58k Pixel), FreeRTOS stabil (10s Laufzeit, Scheduler aktiv, Tasks laufen).

**Offen**:
- Performance-Optimierung (aktuell ~1x Echtzeit)
- FPU Lazy Stacking (EXC_RETURN bit[4]) in exception_entry/return
- Watchface zeigt 00:00 (kein RTC time-of-day)

## Workflow

Nach jedem Fortschritt (neue Peripherals, Bug-Fixes, bestandene Tests):
1. CLAUDE.md aktualisieren (Status, neue Dateien, bekannte Issues)
2. Commit

## Nicht-Ziele

- Cycle-Accuracy
- Vollständige nRF52-Peripherie (nur was InfiniTime braucht)
- BLE Radio-Emulation (SoftDevice-Blob läuft mit, BLE-Pakete gehen nirgends hin)
