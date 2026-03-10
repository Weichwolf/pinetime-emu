# PineTime Emulator

nRF52832 / ARM Cortex-M4F Emulator für PineTime Smartwatch. Ziel: InfiniTime ohne Hardware booten.

## Hardware-Ziel

PineTime: nRF52832 (Cortex-M4F @ 64MHz), 64KB SRAM, 512KB Flash + 4MB ext. SPI NOR.
Display ST7789 240×240 SPI, Touch CST816S I2C, HR HRS3300 I2C, Accel BMA421 I2C.

## Projektstruktur

```
pinetime-emu/
├── src/
│   ├── main.cpp              # EmulatorContext, CLI, SDL2 Main-Loop
│   ├── cpu.hpp               # Cortex-M4F State + Interface
│   ├── cpu.cpp               # Thumb-2 Execution Engine (~1500 Zeilen)
│   ├── bus.hpp               # Device Interface, Bus mit Fast-Path
│   └── device/
│       ├── memory.hpp        # Flash (read-only) + RAM
│       └── nvic.hpp          # NVIC + SCB + SysTick (System Control Space)
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

**PC-Konvention**: `r[15]` = Adresse der aktuellen Instruktion. Lesen als Operand gibt `r[15] + 4`.
**IT-Blöcke**: Condition-Check vor Execution, `advance_it()` danach. 16-bit Instruktionen setzen keine Flags in IT.
**Exceptions**: Cortex-M Stacking (8 Register), EXC_RETURN Magic Values, NVIC-Prioritäten.

## nRF52832 Memory Map

| Adresse | Größe | Gerät |
|---------|-------|-------|
| 0x00000000 | 512KB | Flash |
| 0x10000000 | 4KB | FICR (Factory Info, Stub-Werte) |
| 0x10001000 | 4KB | UICR (Stub) |
| 0x20000000 | 64KB | SRAM |
| 0x40000000 | 512MB | Peripherals (Stub — gibt 0 zurück) |
| 0xE000E000 | 4KB | SCS (NVIC + SCB + SysTick) |

## Test-Konvention

Tests terminieren via BKPT. R0=0 = PASS, R0=N = Test N fehlgeschlagen.

## Status

**CPU**: Thumb-2 Core funktional (16-bit + 32-bit, IT-Blöcke, Exceptions). 10 Basistests bestehen.

**Offen**:
- nRF52 Peripherals: GPIO, SPIM, TWIM, RTC, TIMER, CLOCK, POWER, RADIO
- PineTime Devices: ST7789 (Display→SDL2), CST816S (Touch→Maus), HRS3300, BMA421
- Ext. SPI Flash (4MB)
- FPU (Cortex-M4F Hard-Float)
- Mehr CPU-Tests (Edge-Cases, Interrupts)

## Workflow

Nach jedem Fortschritt (neue Peripherals, Bug-Fixes, bestandene Tests):
1. CLAUDE.md aktualisieren (Status, neue Dateien, bekannte Issues)
2. Commit

## Nicht-Ziele

- Cycle-Accuracy
- Vollständige nRF52-Peripherie (nur was InfiniTime braucht)
- BLE Radio-Emulation (SoftDevice-Blob läuft mit, BLE-Pakete gehen nirgends hin)
