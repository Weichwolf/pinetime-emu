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
│   ├── cpu.cpp               # Thumb-2 Execution Engine (~2200 Zeilen)
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
│       ├── radio.hpp         # BLE Radio (State Machine, TX/RX, Shorts)
│       ├── ppi.hpp           # PPI (Programmable Peripheral Interconnect)
│       ├── saadc.hpp         # SAADC (ADC, simuliert Batteriespannung)
│       ├── temp.hpp          # TEMP (Die-Temperatur, 21°C)
│       ├── rng.hpp           # Random Number Generator (VALRDY events)
│       ├── ecb.hpp           # AES-128 ECB Encryption
│       ├── uarte.hpp         # UARTE (UART mit EasyDMA, TX via Callback)
│       ├── wdt.hpp           # Watchdog (Timeout, tick/next_event, Reload)
│       ├── st7789.hpp        # ST7789 LCD Controller (240×240 RGB565 Framebuffer)
│       ├── i2c_devices.hpp   # CST816S Touch, HRS3300 HR, BMA4xx Accel
│       ├── spi_flash.hpp     # Ext. SPI NOR Flash 4MB (MX25L Protokoll)
│       └── egu.hpp           # Event Generator Unit (Software-Interrupts)
├── tests/
│   ├── arm/                  # ARM-Assembly-Tests (cross-compiled, im Emulator ausgeführt)
│   │   ├── test_basic.s      # CPU-Grundtest (10 Tests, BKPT-terminiert)
│   │   ├── test_alu.s        # ALU-Operationen
│   │   ├── test_thumb32.s    # Thumb-32 Instruktionen
│   │   ├── test_mem.s        # Memory Load/Store
│   │   ├── test_vfp.s        # VFP Floating-Point
│   │   ├── test_it.s         # IT-Blöcke
│   │   ├── test_exceptions.s # Exception Handling
│   │   ├── test_peripherals.s # Peripheral-Register
│   │   ├── test_fpu_exceptions.s # FPU Context Stacking in Exceptions
│   │   ├── test_faults.s     # Fault-Handler (UsageFault, HardFault, CFSR, MPU)
│   │   ├── test_vfp_fused.s  # VFP Fused Multiply-Add (VFMA, VFMS, VFNMA, VFNMS)
│   │   ├── test_vfp_negmul.s # VFP Negated Multiply (VNMUL, VNMLA, VNMLS)
│   │   ├── test.ld           # Linker-Script
│   │   └── Makefile          # Standalone-Build (auch via CMake)
└── CMakeLists.txt
```

## Build

```bash
cmake -B build -G Ninja . && ninja -C build          # Emulator + alle Tests
ctest --test-dir build                                # 35 Suites (23 C++ + 12 ARM)
make -C tests/arm                                     # ARM-Tests standalone
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
| 0x40002000 | UARTE0 (Serial) | uarte.hpp |
| 0x4000B000 | RTC0 | timer.hpp |
| 0x40010000 | WDT | wdt.hpp |
| 0x40011000 | RTC1 | timer.hpp |
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
| 0x40014000–0x40019000 | EGU0–EGU5 (Software-Interrupts) | egu.hpp |
| Ext. SPI | SPI NOR Flash 4MB (MX25L) | spi_flash.hpp |
| Stubs | FPU access port, CCM/AAR | main.cpp |

## Test-Konvention

Tests terminieren via BKPT. R0=0 = PASS, R0=N = Test N fehlgeschlagen.

## Status

**CPU**: Thumb-2 Core + VFPv4-D16 FPU (single+double precision). IT-Blöcke, Exceptions mit FPU Lazy Stacking, LDREX/STREX Exclusive Monitor, Decode-Cache + Block-Chaining (~3x Echtzeit).
**Peripherals**: Alle nRF52-Peripherals implementiert: UARTE, Radio (State Machine), PPI, SAADC, TEMP, RNG, ECB, NVMC, SysTick, NVIC, SPIM, TWIM (ANACK), Timers, RTCs, GPIO/GPIOTE, WDT, EGU0–5, CLOCK/POWER.
**PineTime**: ST7789 (SPI→Framebuffer→SDL2), CST816S Touch (WASD=Swipe, Maus=Tap, Space=Button), HRS3300 (PPG, 75 BPM), BMA4xx (Step Counter). Ext. SPI NOR Flash 4MB (MX25L).
**Firmware**: `firmware/merged-internal.bin` — InfiniTime 1.14.1 + MCUBoot Bootloader.

**InfiniTime Boot**: ✓ Erfolgreich. MCUBoot → InfiniTime Handoff, Display ON, Digital Watchface gerendert (8k/58k Pixel), FreeRTOS stabil (10s Laufzeit, Scheduler aktiv, Tasks laufen).

**Touch/Gesture Input**: ✓ Funktioniert. WASD für Swipe-Gesten (W=Up, S=Down, A=Left, D=Right), Maus für Tap, Space für Button. Touch-IRQ-Kette: CST816S → GPIO Pin 28 (active low) → GPIOTE PORT Event → NVIC IRQ 6 → FreeRTOS Task Notification → Gesture Processing → Display Update.

**Kritische Emulations-Fixes**:
- GPIO SENSE/DETECT: PORT event feuert auf DETECT Rising Edge (nicht auf jede Pin-Änderung). DETECT = OR aller Pins wo SENSE-Condition matched. Auto-Toggle: PIN_CNF Read toggled SENSE intern wenn Condition matched (InfiniTime's nrfx_gpiote toggled SENSE nicht selbst). Firmware die SENSE explizit schreibt: Write überschreibt Auto-Toggle.
- CST816S Action Field: Register 0x03 Bits [7:6] korrekt setzen (0=Down, 1=Up). Nötig damit InfiniTime Touch-Releases erkennt.
- WFI/WFE Wakeup trotz PRIMASK: Pending Interrupts wecken CPU aus WFI/WFE auch bei PRIMASK=1.
- NVIC Event Register: Interrupts setzen event_reg für WFE-Wakeup.

**Tests**: 35 Suites (23 C++ + 12 ARM).

## Roadmap: Produktiver Emulator

### Phase 1: CPU härten — ✓ abgeschlossen
Ziel: Korrektheit gegen ISA-Spec, nicht nur gegen InfiniTime.
- ✓ FPU Lazy Stacking (EXC_RETURN bit[4], CONTROL.FPCA, S0-S15+FPSCR Stacking/Restore)
- ✓ Exception Tail-Chaining (funktional via irq_pending re-check)
- ✓ LDREX/STREX Exclusive Monitor (exclusive_addr_ Tracking, CLREX, Exception-Clear)
- ✓ Encoding Edge Cases (shift_c: amount=32/33+, LSL/LSR/ASR/ROR korrekt)
- ✓ MPU Stub (8 Regions, akzeptiert Config, kein Enforcement — reicht für FreeRTOS)
- ✓ Fault-Handler getrennt (UsageFault/BusFault/MemManage mit SHCSR-Enable, CFSR/HFSR W1C, Escalation zu HardFault)
- ✓ 35 Testsuites (23 C++ + 12 ARM), InfiniTime-Boot verifiziert

### Phase 2: Event-Scheduler — ✓ abgeschlossen
Ziel: Timing-korrekte Peripheral-Interaktion statt 10k-Zyklus-Batches.
- ✓ `Device::next_event(now) → uint64_t` virtuelle Methode in bus.hpp
- ✓ CPU läuft bis min(next_event, 10k-batch, frame_end) — event-driven
- ✓ Timer: next_event nur bei aktivem Interrupt/Short auf relevante CC-Matches
- ✓ RTC: next_event bei TICK-Interrupt oder CC-Match mit enabletem Interrupt
- ✓ SysTick: next_event = now + CVR (verbleibende Zyklen bis Underflow)
- ✓ RNG/TEMP: next_event = now + delay (Verzögerung bis VALRDY/DATARDY)
- ✓ PPI: kein next_event (pollt Event-Register, läuft bei jedem tick)
- ✓ `EmulatorContext::next_peripheral_event()` sammelt frühestes Event
- ✓ Headless + Interactive Loop beide event-driven
- ✓ InfiniTime Boot identisch (8321px, 5116 Ticks), Performance ~32M cy/s

### Phase 3: Peripherals vertiefen — ✓ abgeschlossen
Ziel: Korrektheit für beliebige nRF52-Firmware, nicht nur InfiniTime.
- ✓ UARTE (EasyDMA TX, TX-Callback, STARTRX/STOPRX Events)
- ✓ TWIM: ANACK-Erkennung bei unbekannter I2C-Adresse (errorsrc_ + ev_error_)
- ✓ RADIO: Korrekte State Machine (DISABLED=0, RXRU=1, RXIDLE=2, RX=3, TXRU=9, TXIDLE=10, TX=11, TXDISABLE=12), DISABLED_TXEN/RXEN Shorts für TX/RX-Chaining
- ✓ WDT: Timeout nach (CRV+1)*1953 Zyklen, tick/next_event, CRV/RREN/CONFIG locked nach START
- ✓ GPIO SENSE: Korrekte DETECT Rising-Edge-Semantik + Auto-Toggle bei PIN_CNF Read (Kompatibilität mit Firmware die SENSE nicht selbst toggled)
- — NVMC: Instant genügt (kein Timing nötig für InfiniTime/MCUBoot DFU)
- — Timer Shorts: Waren schon komplett (COMPARE_CLEAR + COMPARE_STOP)
- — DMA Timing: Instant genügt für alle aktuellen Firmware

### Phase 4: Performance — ✓ abgeschlossen
Ziel: >3x Echtzeit (Interpreter-Limit ohne JIT).
- ✓ Cached Decode (OpClass pro Halfword → Single-Level Dispatch statt 3-Level)
- ✓ Direct Flash Access (Pointer statt Bus::read16 → kein Virtual Call overhead)
- ✓ Inner Loop (irq_pending/breakpoint nur am Loop-Rand, nicht pro Instruktion)
- ✓ O3 + LTO (Link-Time Optimization für Cross-TU Inlining)
- ✓ Cache-Invalidierung bei NVMC Flash Write/Erase
- Ergebnis: 2.3x → 3.1x Echtzeit (36% schneller, ~7ns/Instruktion)
- PGO getestet, kein weiterer Gewinn (<1%)

### Phase 5: BLE via HCI over TCP — ⬚ offen
Ziel: BLE-Kommunikation mit externem Host-Stack (Gadgetbridge, Bumble, BlueZ).

**Architektur:**
- Emulator = HCI Controller (TCP Server :9001)
- Externer Host-Stack = HCI Client
- Protokoll: HCI H4 Framing (1 Byte Packet Indicator + HCI Packet)
- SoftDevice steuert RADIO direkt → Übersetzung RADIO-Register → HCI nötig

**Schichten:**
1. **RADIO Capture** — TX-Pakete abfangen wenn RADIO in TX state, RX-Pakete injizieren
2. **Link Layer** — BLE LL PDU Parsing (ADV_IND, CONNECT_IND, LL_DATA)
3. **HCI Transport** — H4 Framing, Command/Event/ACL Multiplexing
4. **TCP Server** — Non-blocking Socket, accept/read/write in tick()

**Herausforderungen:**
- SoftDevice ist Closed-Source Blob, steuert RADIO-Register direkt
- Timing: BLE erwartet µs-genaue Antworten (Connection Interval, T_IFS=150µs)
- State Tracking: SoftDevice's interner State muss aus Register-Writes inferiert werden
- Advertising: SoftDevice setzt RADIO auf ADV-Channels (37/38/39), sendet ADV_IND
- Connection: Nach CONNECT_IND wechselt SoftDevice auf Data Channels

**Gegenstellen:**
- `bumble` (Python BLE Stack) — flexibelste Option, HCI over TCP nativ
- `BlueZ vhci` — Linux Kernel HCI Interface, braucht root
- Eigene Test-App — minimaler HCI Client für Connection+GATT

**Schritte:**
- ⬚ TCP Server Skeleton (non-blocking, in main loop integriert)
- ⬚ RADIO TX Capture: Paket aus PACKETPTR lesen wenn END event
- ⬚ BLE ADV PDU Parsing (ADV_IND, SCAN_REQ, SCAN_RSP)
- ⬚ HCI Event Generation (LE Advertising Report)
- ⬚ HCI Command Handling (Reset, LE Set Scan, LE Create Connection)
- ⬚ Connection State: CONNECT_IND → Data Channel PDUs → HCI ACL
- ⬚ RADIO RX Injection: HCI ACL → LL PDU → PACKETPTR schreiben, RADIO Events feuern
- ⬚ Integration Test mit Bumble (Scan → Connect → GATT Read)

### Bestandsaufnahme (was steht, was nicht)
| Komponente | Status | Produktiv-tauglich? |
|-----------|--------|---------------------|
| Bus-Architektur | ✓ | Ja — Fast/Slow-Path korrekt |
| Device-Interface | ✓ | Ja — read/write/tick erweiterbar |
| EmulatorContext | ✓ | Ja — zentralisierte Verdrahtung |
| Thumb-2 Core | ✓ ~95% | Fast — Shift Edge Cases gefixt, Exclusive Monitor |
| VFP/FPU | ✓ ~95% | Fast — Lazy Stacking implementiert, FPCA korrekt |
| Exception Model | ✓ ~98% | Ja — FPU Stacking, Faults, MPU, Exclusive Monitor |
| nRF52 Peripherals | ✓ Happy Path | Nein — Error Handling, Timing fehlt |
| Test-Infrastruktur | ✓ | Ja — C++ + ARM dual-system |

## Workflow

Nach jedem Fortschritt (neue Peripherals, Bug-Fixes, bestandene Tests):
1. CLAUDE.md aktualisieren (Status, neue Dateien, bekannte Issues)
2. Commit

