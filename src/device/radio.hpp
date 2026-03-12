#pragma once
#include "../bus.hpp"
#include "nvic.hpp"
#include "ppi.hpp"
#include <array>

namespace pt {

// nRF52 RADIO at 0x40001000
// BLE radio with TX capture and RX injection for HCI bridging.
// Tick-based state machine: TXEN/RXEN schedule ramp-up, tick() advances states.
// This prevents synchronous event chains from firing while PRIMASK=1.
class Radio : public Device {
    // Events
    uint32_t ev_ready_ = 0;
    uint32_t ev_address_ = 0;
    uint32_t ev_payload_ = 0;
    uint32_t ev_end_ = 0;
    uint32_t ev_disabled_ = 0;
    uint32_t ev_devmatch_ = 0;
    uint32_t ev_devmiss_ = 0;
    uint32_t ev_rssiend_ = 0;
    uint32_t ev_bcmatch_ = 0;
    uint32_t ev_crcok_ = 0;
    uint32_t ev_crcerror_ = 0;

    uint32_t shorts_ = 0;
    uint32_t inten_ = 0;

    // Config registers
    uint32_t packetptr_ = 0;
    uint32_t frequency_ = 0;
    uint32_t txpower_ = 0;
    uint32_t mode_ = 0;
    uint32_t pcnf0_ = 0;
    uint32_t pcnf1_ = 0;
    uint32_t base0_ = 0, base1_ = 0;
    uint32_t prefix0_ = 0, prefix1_ = 0;
    uint32_t txaddress_ = 0;
    uint32_t rxaddresses_ = 0;
    uint32_t crccnf_ = 0;
    uint32_t crcpoly_ = 0;
    uint32_t crcinit_ = 0;
    uint32_t tifs_ = 0;
    uint32_t rssisample_ = 0xFFFFFFFF;
    uint32_t state_ = 0;  // 0=Disabled
    uint32_t datawhiteiv_ = 0;
    std::array<uint32_t, 8> bcc_{};
    std::array<uint32_t, 8> dab_{};
    std::array<uint32_t, 8> dap_{};
    uint32_t dacnf_ = 0;
    uint32_t modecnf0_ = 0;
    uint32_t power_ = 1;

    NVIC* nvic_ = nullptr;
    uint32_t irq_ = 0;
    Bus* bus_ = nullptr;
    PPI* ppi_ = nullptr;

    // Cycle counter and event register from CPU
    uint64_t* cpu_cy_ = nullptr;
    bool* cpu_event_reg_ = nullptr;

    // Deferred state machine
    enum Phase { IDLE, RAMPING_TX, RAMPING_RX, TRANSMITTING, TX_END, DISABLING };
    Phase phase_ = IDLE;
    uint64_t phase_end_cy_ = 0;

    // Timing (cycles at 64MHz). Event-driven scheduling via CPU yield_at ensures
    // the main loop ticks peripherals at the right cycle, so these can be realistic.
    static constexpr uint64_t RAMP_CYCLES    = 2560;  // 40µs ramp-up
    static constexpr uint64_t ADDR_CYCLES    = 2560;  // 40µs preamble + access address
    static constexpr uint64_t PDU_CYCLES     = 2560;  // 40µs PDU body
    static constexpr uint64_t DISABLE_CYCLES = 384;   // 6µs disable ramp-down

    // TX capture callback
    using TxHandler = void(*)(void*, const uint8_t*, size_t, uint8_t);
    TxHandler tx_handler_ = nullptr;
    void* tx_handler_ctx_ = nullptr;
    uint32_t tx_capture_ptr_ = 0; // PACKETPTR saved before ISR runs at ADDRESS

    // Pending RX injection
    struct PendingRx {
        uint8_t data[264];
        size_t len = 0;
        bool pending = false;
    } pending_rx_;

    uint32_t txen_count_ = 0, rxen_count_ = 0;

public:
    void set_nvic(NVIC* nvic, uint32_t irq) { nvic_ = nvic; irq_ = irq; }
    void set_bus(Bus* b) { bus_ = b; }
    void set_ppi(PPI* p) { ppi_ = p; }
    void set_cycles(uint64_t* cy, bool* event_reg = nullptr) {
        cpu_cy_ = cy;
        cpu_event_reg_ = event_reg;
    }
    void set_tx_handler(TxHandler h, void* ctx) { tx_handler_ = h; tx_handler_ctx_ = ctx; }

    uint32_t get_state() const { return state_; }
    uint32_t get_frequency() const { return frequency_; }
    uint32_t get_packetptr() const { return packetptr_; }
    uint32_t get_datawhiteiv() const { return datawhiteiv_ & 0x3F; }
    uint32_t txen_count() const { return txen_count_; }
    uint32_t rxen_count() const { return rxen_count_; }

    void queue_rx(const uint8_t* data, size_t len) {
        if (len > sizeof(pending_rx_.data)) return;
        memcpy(pending_rx_.data, data, len);
        pending_rx_.len = len;
        pending_rx_.pending = true;
    }

    // Check for pending RX injection (called from BleHci poll)
    bool check_pending_rx() {
        if (!pending_rx_.pending || state_ != RX) return false;
        pending_rx_.pending = false;
        inject_rx(pending_rx_.data, pending_rx_.len);
        return true;
    }

    uint32_t read(uint32_t addr, Width) override {
        // Lazy state machine advance: if CPU reads an event/state register and
        // the current deferred phase has elapsed, complete it now.
        // This breaks polling deadlocks where tick() can't run because the
        // CPU is stuck reading events/state in a tight loop (WFE loop for
        // EVENTS_READY, ISR polling loop for STATE==DISABLED).
        if (phase_ != IDLE && deferred() &&
            ((addr >= 0x100 && addr <= 0x134) || addr == 0x550)) {
            advance_phase_if_due();
        }
        switch (addr) {
        case 0x100: return ev_ready_;
        case 0x104: return ev_address_;
        case 0x108: return ev_payload_;
        case 0x10C: return ev_end_;
        case 0x110: return ev_disabled_;
        case 0x114: return ev_devmatch_;
        case 0x118: return ev_devmiss_;
        case 0x11C: return ev_rssiend_;
        case 0x128: return ev_bcmatch_;
        case 0x130: return ev_crcok_;
        case 0x134: return ev_crcerror_;
        case 0x200: return shorts_;
        case 0x300: return inten_;
        case 0x304: return inten_;
        case 0x308: return inten_;  // INTENCLR reads as current mask
        case 0x504: return packetptr_;
        case 0x508: return frequency_;
        case 0x50C: return txpower_;
        case 0x510: return mode_;
        case 0x514: return pcnf0_;
        case 0x518: return pcnf1_;
        case 0x51C: return base0_;
        case 0x520: return base1_;
        case 0x524: return prefix0_;
        case 0x528: return prefix1_;
        case 0x52C: return txaddress_;
        case 0x530: return rxaddresses_;
        case 0x534: return crccnf_;
        case 0x538: return crcpoly_;
        case 0x53C: return crcinit_;
        case 0x544: return tifs_;
        case 0x548: return rssisample_;
        case 0x550: return state_;
        case 0x554: return datawhiteiv_;
        case 0x650: return dacnf_;
        case 0x5B0: return modecnf0_;
        case 0xFFC: return power_;
        }
        if (addr >= 0x600 && addr < 0x620) return dab_[(addr-0x600)/4];
        if (addr >= 0x620 && addr < 0x640) return dap_[(addr-0x620)/4];
        return 0;
    }

    void write(uint32_t addr, Width, uint32_t val) override {
        switch (addr) {
        case 0x000:
            if (val) do_txen();
            break;
        case 0x004:
            if (val) do_rxen();
            break;
        case 0x008: if (val) do_start(); break;
        case 0x00C: if (val) do_stop(); break;
        case 0x010: if (val) do_disable(); break;
        case 0x014: break; // RSSISTART
        case 0x018: break; // RSSISTOP
        case 0x01C: break; // BCSTART
        case 0x020: break; // BCSTOP
        case 0x024: break; // EDSTART
        case 0x028: break; // EDSTOP
        case 0x02C: break; // CCASTART
        case 0x030: break; // CCASTOP
        case 0x100: ev_ready_ = val; break;
        case 0x104: ev_address_ = val; break;
        case 0x108: ev_payload_ = val; break;
        case 0x10C: ev_end_ = val; break;
        case 0x110: ev_disabled_ = val; break;
        case 0x114: ev_devmatch_ = val; break;
        case 0x118: ev_devmiss_ = val; break;
        case 0x11C: ev_rssiend_ = val; break;
        case 0x128: ev_bcmatch_ = val; break;
        case 0x130: ev_crcok_ = val; break;
        case 0x134: ev_crcerror_ = val; break;
        case 0x200: shorts_ = val; break;
        case 0x300: inten_ = val; break;
        case 0x304:
            inten_ |= val;
            fire_irq(); // Re-check: event may already be set
            break;
        case 0x308: inten_ &= ~val; break;
        case 0x504: packetptr_ = val; break;
        case 0x508: frequency_ = val; break;
        case 0x50C: txpower_ = val; break;
        case 0x510: mode_ = val; break;
        case 0x514: pcnf0_ = val; break;
        case 0x518: pcnf1_ = val; break;
        case 0x51C: base0_ = val; break;
        case 0x520: base1_ = val; break;
        case 0x524: prefix0_ = val; break;
        case 0x528: prefix1_ = val; break;
        case 0x52C: txaddress_ = val; break;
        case 0x530: rxaddresses_ = val; break;
        case 0x534: crccnf_ = val; break;
        case 0x538: crcpoly_ = val; break;
        case 0x53C: crcinit_ = val; break;
        case 0x544: tifs_ = val; break;
        case 0x554: datawhiteiv_ = val; break;
        case 0x650: dacnf_ = val; break;
        case 0x5B0: modecnf0_ = val; break;
        case 0xFFC: power_ = val & 1; break;
        }
        if (addr >= 0x600 && addr < 0x620) { dab_[(addr-0x600)/4] = val; return; }
        if (addr >= 0x620 && addr < 0x640) { dap_[(addr-0x620)/4] = val; return; }
    }

    // Tick-based state machine — advances radio through deferred phases
    std::optional<Interrupt> tick(uint64_t cycles) override {
        if (phase_ == IDLE) return {};
        if (cycles < phase_end_cy_) return {};

        bool irq_needed = false;

        switch (phase_) {
        case RAMPING_TX:
            state_ = TXIDLE;
            ev_ready_ = 1;
            fire_ppi(0x100); // READY
            if (shorts_ & (1 << 0)) { // READY_START
                state_ = TX;
                phase_ = TRANSMITTING;
                phase_end_cy_ = cycles + ADDR_CYCLES;
            } else {
                phase_ = IDLE;
            }
            irq_needed = true;
            break;

        case RAMPING_RX:
            state_ = RXIDLE;
            ev_ready_ = 1;
            fire_ppi(0x100); // READY
            if (shorts_ & (1 << 0)) { // READY_START
                state_ = RX;
            }
            irq_needed = true;
            phase_ = IDLE;
            // Inject pending RX after phase is settled
            if (state_ == RX && pending_rx_.pending) {
                pending_rx_.pending = false;
                inject_rx(pending_rx_.data, pending_rx_.len);
            }
            break;

        case TRANSMITTING: {
            // ADDRESS fires mid-TX (after preamble + access address match).
            // ISR runs here and may reconfigure SHORTS for TX→RX turnaround.
            // Save PACKETPTR before ISR runs (ISR may change it for subsequent RX)
            tx_capture_ptr_ = packetptr_;
            ev_address_ = 1;
            fire_ppi(0x104); // ADDRESS
            phase_ = TX_END;
            phase_end_cy_ = cycles + PDU_CYCLES;
            irq_needed = true;
            break;
        }

        case TX_END: {
            // TX complete — fire END events, capture packet
            ev_payload_ = 1; fire_ppi(0x108);
            ev_end_ = 1;     fire_ppi(0x10C);
            capture_tx();
            state_ = TXIDLE;
            // Check SHORTS now — ISR may have changed them since ADDRESS
            if (shorts_ & (1 << 1)) { // END_DISABLE
                enter_disable(cycles);
            } else {
                phase_ = IDLE;
            }
            irq_needed = true;
            break;
        }

        case DISABLING:
            complete_disable();
            irq_needed = true;
            break;

        default:
            phase_ = IDLE;
            break;
        }

        if (irq_needed) {
            fire_irq();
            if (cpu_event_reg_) *cpu_event_reg_ = true;
        }
        return {};
    }

    uint64_t next_event(uint64_t) const override {
        if (phase_ != IDLE) return phase_end_cy_;
        return UINT64_MAX;
    }

private:
    uint64_t now() const { return cpu_cy_ ? *cpu_cy_ : 0; }

    // Advance deferred state machine if current phase is due.
    // Called lazily from read() so CPU sees events without waiting for tick().
    // Only completes ONE phase per call — subsequent phases complete via
    // normal tick() in the main loop, giving firmware time to configure
    // state between events (e.g. phy_transition between READY and DISABLED).
    void advance_phase_if_due() {
        if (now() < phase_end_cy_) return;
        tick(phase_end_cy_);
    }

    // nRF52 RADIO states
    enum State {
        DISABLED = 0, RXRU = 1, RXIDLE = 2, RX = 3, RXDISABLE = 4,
        TXRU = 9, TXIDLE = 10, TX = 11, TXDISABLE = 12
    };

    void fire_irq() {
        if (!nvic_) return;
        bool pending = false;
        pending |= (inten_ & (1<<0)) && ev_ready_;
        pending |= (inten_ & (1<<1)) && ev_address_;
        pending |= (inten_ & (1<<2)) && ev_payload_;
        pending |= (inten_ & (1<<3)) && ev_end_;
        pending |= (inten_ & (1<<4)) && ev_disabled_;
        if (pending) nvic_->set_pending(irq_);
    }

    void fire_ppi(uint32_t event_offset) {
        if (ppi_) ppi_->on_event(0x40001000 + event_offset);
    }

    // Compute RAM header size from PCNF0: S0 + LENGTH + S1 bytes before payload
    // Returns at least 2 (S0+LENGTH) as fallback when PCNF0 is unconfigured.
    uint32_t pdu_header_bytes() const {
        uint32_t s0_bytes = (pcnf0_ >> 8) & 1;
        uint32_t len_bytes = ((pcnf0_ & 0xF) + 7) / 8;
        uint32_t s1_len = (pcnf0_ >> 16) & 0xF;
        uint32_t s1_incl = (pcnf0_ >> 20) & 1;
        uint32_t s1_bytes = s1_incl ? ((s1_len + 7) / 8 > 0 ? (s1_len + 7) / 8 : 1) : 0;
        uint32_t total = s0_bytes + len_bytes + s1_bytes;
        return total ? total : 2;
    }

    void capture_tx() {
        if (!tx_handler_ || !bus_) return;
        uint32_t hdr_bytes = pdu_header_bytes();
        uint8_t pdu[264];
        uint8_t s0 = bus_->read8(tx_capture_ptr_);
        uint8_t pdu_len = bus_->read8(tx_capture_ptr_ + 1);
        pdu[0] = s0;
        pdu[1] = pdu_len;
        if (pdu_len > 254) pdu_len = 254;
        // Read payload starting after S0+LENGTH+S1 (skip S1)
        for (int i = 0; i < pdu_len; i++)
            pdu[2 + i] = bus_->read8(tx_capture_ptr_ + hdr_bytes + i);
        tx_handler_(tx_handler_ctx_, pdu, 2 + pdu_len, datawhiteiv_ & 0x3F);
    }

    void do_tx_packet() {
        state_ = TX;
        tx_capture_ptr_ = packetptr_;
        ev_address_ = 1; fire_ppi(0x104);
        ev_payload_ = 1; fire_ppi(0x108);
        ev_end_ = 1;     fire_ppi(0x10C);
        capture_tx();
        state_ = TXIDLE;
    }

public:
    void inject_rx(const uint8_t* pdu, size_t len) {
        if (state_ != RX || !bus_) return;
        // Write PDU to PACKETPTR in nRF52 RAM format: S0 + LENGTH + S1 + Payload
        // PDU input format: [header, length, payload...]
        // RAM format: [S0=header, LENGTH=length, S1=0x00 (if S1INCL), payload...]
        uint32_t hdr_bytes = pdu_header_bytes();
        bus_->write8(packetptr_, pdu[0]);     // S0
        bus_->write8(packetptr_ + 1, pdu[1]); // LENGTH
        // Write S1 byte(s) if present
        for (uint32_t i = 2; i < hdr_bytes; i++)
            bus_->write8(packetptr_ + i, 0x00);
        // Write payload after header
        for (size_t i = 2; i < len; i++)
            bus_->write8(packetptr_ + hdr_bytes + (i - 2), pdu[i]);
        ev_address_ = 1;  fire_ppi(0x104);
        ev_bcmatch_ = 1;  fire_ppi(0x128);
        ev_payload_ = 1;  fire_ppi(0x108);
        ev_end_ = 1;      fire_ppi(0x10C);
        ev_crcok_ = 1;    fire_ppi(0x130);
        rssisample_ = 0x40;
        // Fire IRQ for any enabled event (ADDRESS, END, etc.)
        fire_irq();
        state_ = RXIDLE;
        if (shorts_ & (1u << 1)) { // END_DISABLE
            enter_disable(now());
        }
    }

private:
    void enter_disable(uint64_t cy) {
        if (state_ >= TXRU) state_ = TXDISABLE;
        else if (state_ >= RXRU) state_ = RXDISABLE;
        else state_ = DISABLED; // already disabled
        phase_ = DISABLING;
        phase_end_cy_ = cy + DISABLE_CYCLES;
    }

    void complete_disable() {
        state_ = DISABLED;
        ev_disabled_ = 1;
        fire_ppi(0x110); // DISABLED event
        phase_ = IDLE;

        // DISABLED shorts for TX/RX turnaround (NimBLE uses these)
        if (shorts_ & (1 << 2)) {       // DISABLED_TXEN
            do_txen();
        } else if (shorts_ & (1 << 3)) { // DISABLED_RXEN
            do_rxen();
        }
    }

    bool deferred() const { return cpu_cy_ != nullptr; }

    void do_txen() {
        if (state_ >= TXRU && state_ <= TX) return; // Already in TX path
        txen_count_++;
        state_ = TXRU;
        if (deferred()) {
            phase_ = RAMPING_TX;
            phase_end_cy_ = now() + RAMP_CYCLES;
        } else {
            // Synchronous mode (tests without cycle counter)
            state_ = TXIDLE;
            ev_ready_ = 1;
            fire_ppi(0x100);
            if (shorts_ & (1 << 0)) do_start();
            fire_irq();
        }
    }

    void do_rxen() {
        if (state_ >= RXRU && state_ <= RX) return; // Already in RX path
        rxen_count_++;
        state_ = RXRU;
        if (deferred()) {
            phase_ = RAMPING_RX;
            phase_end_cy_ = now() + RAMP_CYCLES;
        } else {
            state_ = RXIDLE;
            ev_ready_ = 1;
            fire_ppi(0x100);
            if (shorts_ & (1 << 0)) do_start();
            fire_irq();
        }
    }

    void do_start() {
        if (state_ == TXIDLE) {
            do_tx_packet();
            if (shorts_ & (1 << 1)) { // END_DISABLE
                if (deferred()) {
                    enter_disable(now());
                } else {
                    do_disable_sync();
                }
            } else {
                fire_irq();
            }
        } else if (state_ == RXIDLE) {
            state_ = RX;
        }
    }

    void do_stop() {
        if (state_ == RX) state_ = RXIDLE;
        else if (state_ == TX) state_ = TXIDLE;
        if (deferred()) {
            do_disable();
        } else {
            do_disable_sync();
        }
    }

    void do_disable() {
        phase_ = IDLE;
        enter_disable(now());
        // Complete immediately for explicit DISABLE task
        complete_disable();
        fire_irq();
    }

    // Synchronous disable for legacy mode (no cycle counter)
    bool in_disable_sync_ = false;
    void do_disable_sync() {
        if (in_disable_sync_) return;
        in_disable_sync_ = true;
        if (state_ >= TXRU) state_ = TXDISABLE;
        else if (state_ >= RXRU) state_ = RXDISABLE;
        state_ = DISABLED;
        ev_disabled_ = 1;
        fire_ppi(0x110);
        fire_irq();
        if (shorts_ & (1 << 2)) do_txen();
        else if (shorts_ & (1 << 3)) do_rxen();
        in_disable_sync_ = false;
    }
};

} // namespace pt
