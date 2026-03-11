#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace pt {

// Virtual companion — ATT/GATT client that auto-syncs time, music, weather, notifications
class BleCompanion {
public:
    void on_connected() {
        state_ = MTU;
        pending_[0] = 0x02; // Exchange MTU Request
        pending_[1] = 185; pending_[2] = 0;
        pending_len_ = 3;
    }

    void on_att_received(const uint8_t* att, size_t len) {
        if (len < 1) return;
        uint8_t opcode = att[0];

        // Always respond to MTU Request from device
        if (opcode == 0x02 && len >= 3) {
            pending_[0] = 0x03;
            pending_[1] = 185; pending_[2] = 0;
            pending_len_ = 3;
            mtu_ = 185;
            if (state_ == MTU) {
                state_ = SVC_DISC;
                // We'll start discovery after sending this response
                // by queuing in next exchange
                need_svc_disc_start_ = true;
            }
            return;
        }

        switch (state_) {
        case MTU:
            if (opcode == 0x03 && len >= 3) { // MTU Response
                mtu_ = att[1] | (att[2] << 8);
                if (mtu_ < 23) mtu_ = 23;
                if (mtu_ > 247) mtu_ = 247;
                start_svc_discovery();
            }
            break;
        case SVC_DISC:
            if (opcode == 0x11) parse_svc_disc(att, len);
            else if (opcode == 0x01) finish_svc_discovery();
            break;
        case CHAR_DISC:
            if (opcode == 0x09) parse_char_disc(att, len);
            else if (opcode == 0x01) { char_phase_++; next_char_disc_phase(); }
            break;
        case WRITES:
            if (opcode == 0x13 || opcode == 0x01) { // Write Response or Error
                write_idx_++;
                send_next_write();
            }
            break;
        default: break;
        }
    }

    bool has_pending() {
        // Deferred service discovery start (after responding to device's MTU Request)
        if (need_svc_disc_start_ && pending_len_ == 0) {
            need_svc_disc_start_ = false;
            start_svc_discovery();
        }
        return pending_len_ > 0;
    }

    size_t get_pending(uint8_t* out) {
        size_t n = pending_len_;
        memcpy(out, pending_, n);
        pending_len_ = 0;
        return n;
    }

    bool is_done() const { return state_ == DONE; }

private:
    enum State { IDLE, MTU, SVC_DISC, CHAR_DISC, WRITES, DONE } state_ = IDLE;

    uint16_t mtu_ = 23;
    bool need_svc_disc_start_ = false;

    // Service discovery
    uint16_t disc_next_ = 1;
    struct Svc { uint16_t start = 0, end = 0; bool found = false; };
    Svc cts_, music_, ans_, weather_;

    // Characteristic discovery
    int char_phase_ = 0; // 0=CTS, 1=Music, 2=ANS, 3=Weather

    // Discovered characteristic value handles
    uint16_t h_time_ = 0;
    uint16_t h_track_ = 0, h_artist_ = 0, h_album_ = 0, h_mstatus_ = 0;
    uint16_t h_alert_ = 0;
    uint16_t h_weather_ = 0;

    // Write queue
    struct WriteOp { uint16_t handle; uint8_t data[200]; uint8_t len; };
    WriteOp writes_[8];
    int num_writes_ = 0, write_idx_ = 0;

    uint8_t pending_[256];
    size_t pending_len_ = 0;

    // InfiniTime custom UUID base (LE bytes 0-11)
    static bool match_base(const uint8_t* u) {
        static constexpr uint8_t B[12] = {
            0xd0,0x42,0x19,0x3a,0x3b,0x43,0x23,0x8e,0xfe,0x48,0xfc,0x78
        };
        return memcmp(u, B, 12) == 0;
    }

    // ================================================================
    // Service Discovery (Read By Group Type, UUID=0x2800)
    // ================================================================
    void start_svc_discovery() {
        state_ = SVC_DISC;
        disc_next_ = 1;
        build_svc_disc_req(1, 0xFFFF);
    }

    void build_svc_disc_req(uint16_t start, uint16_t end) {
        pending_[0] = 0x10; // Read By Group Type Request
        pending_[1] = start; pending_[2] = start >> 8;
        pending_[3] = end; pending_[4] = end >> 8;
        pending_[5] = 0x00; pending_[6] = 0x28; // UUID 0x2800
        pending_len_ = 7;
    }

    void parse_svc_disc(const uint8_t* att, size_t len) {
        uint8_t entry_len = att[1];
        uint16_t last_end = 0;
        for (size_t pos = 2; pos + entry_len <= len; pos += entry_len) {
            uint16_t h = att[pos] | (att[pos+1] << 8);
            uint16_t e = att[pos+2] | (att[pos+3] << 8);
            last_end = e;
            if (entry_len == 6) { // 16-bit UUID
                uint16_t uuid = att[pos+4] | (att[pos+5] << 8);
                if (uuid == 0x1805) cts_ = {h, e, true};
                else if (uuid == 0x1811) ans_ = {h, e, true};
            } else if (entry_len == 20) { // 128-bit UUID
                const uint8_t* u = att + pos + 4;
                if (match_base(u) && u[12] == 0 && u[13] == 0) {
                    uint16_t sid = u[14] | (u[15] << 8);
                    if (sid == 0x0000) music_ = {h, e, true};
                    else if (sid == 0x0004) weather_ = {h, e, true};
                }
            }
        }
        if (last_end < 0xFFFF) build_svc_disc_req(last_end + 1, 0xFFFF);
        else finish_svc_discovery();
    }

    void finish_svc_discovery() {
        fprintf(stderr, "[Companion] Services: CTS=%s Music=%s ANS=%s Weather=%s\n",
                cts_.found?"Y":"N", music_.found?"Y":"N", ans_.found?"Y":"N", weather_.found?"Y":"N");
        char_phase_ = 0;
        state_ = CHAR_DISC;
        next_char_disc_phase();
    }

    // ================================================================
    // Characteristic Discovery (Read By Type, UUID=0x2803)
    // ================================================================
    void next_char_disc_phase() {
        while (char_phase_ < 4) {
            Svc* s = svc_for_phase(char_phase_);
            if (s && s->found) {
                build_char_disc_req(s->start, s->end);
                return;
            }
            char_phase_++;
        }
        prepare_writes();
    }

    Svc* svc_for_phase(int p) {
        switch (p) { case 0: return &cts_; case 1: return &music_; case 2: return &ans_; case 3: return &weather_; }
        return nullptr;
    }

    void build_char_disc_req(uint16_t start, uint16_t end) {
        pending_[0] = 0x08; // Read By Type Request
        pending_[1] = start; pending_[2] = start >> 8;
        pending_[3] = end; pending_[4] = end >> 8;
        pending_[5] = 0x03; pending_[6] = 0x28; // UUID 0x2803 (Characteristic Declaration)
        pending_len_ = 7;
    }

    void parse_char_disc(const uint8_t* att, size_t len) {
        uint8_t entry_len = att[1];
        uint16_t last_handle = 0;
        for (size_t pos = 2; pos + entry_len <= len; pos += entry_len) {
            uint16_t handle = att[pos] | (att[pos+1] << 8);
            // uint8_t props = att[pos+2];
            uint16_t val_h = att[pos+3] | (att[pos+4] << 8);
            last_handle = handle;

            if (entry_len == 7) { // 16-bit UUID char
                uint16_t uuid = att[pos+5] | (att[pos+6] << 8);
                if (char_phase_ == 0 && uuid == 0x2A2B) h_time_ = val_h;
                else if (char_phase_ == 2 && uuid == 0x2A46) h_alert_ = val_h;
            } else if (entry_len == 21) { // 128-bit UUID char
                const uint8_t* u = att + pos + 5;
                if (match_base(u)) {
                    uint16_t cid = u[12] | (u[13] << 8);
                    uint16_t sid = u[14] | (u[15] << 8);
                    if (char_phase_ == 1 && sid == 0) { // Music
                        if (cid == 1) h_track_ = val_h;
                        else if (cid == 2) h_artist_ = val_h;
                        else if (cid == 3) h_album_ = val_h;
                        else if (cid == 4) h_mstatus_ = val_h;
                    } else if (char_phase_ == 3 && sid == 4) { // Weather
                        if (cid == 1) h_weather_ = val_h;
                    }
                }
            }
        }
        Svc* s = svc_for_phase(char_phase_);
        if (s && last_handle < s->end)
            build_char_disc_req(last_handle + 1, s->end);
        else { char_phase_++; next_char_disc_phase(); }
    }

    // ================================================================
    // Write Operations
    // ================================================================
    void prepare_writes() {
        num_writes_ = write_idx_ = 0;

        if (h_time_) add_write_time();
        if (h_track_) add_write_str(h_track_, "Emulator");
        if (h_artist_) add_write_str(h_artist_, "PineTime");
        if (h_album_) add_write_str(h_album_, "InfiniTime");
        if (h_mstatus_) add_write_byte(h_mstatus_, 0x01); // Playing
        if (h_alert_) add_write_notification();
        if (h_weather_) add_write_weather();

        state_ = WRITES;
        send_next_write();
    }

    void send_next_write() {
        if (write_idx_ >= num_writes_) {
            state_ = DONE;
            fprintf(stderr, "[Companion] Sync complete (%d writes)\n", num_writes_);
            return;
        }
        auto& w = writes_[write_idx_];
        pending_[0] = 0x12; // Write Request
        pending_[1] = w.handle; pending_[2] = w.handle >> 8;
        memcpy(pending_ + 3, w.data, w.len);
        pending_len_ = 3 + w.len;
    }

    void add_write_time() {
        auto& w = writes_[num_writes_++];
        w.handle = h_time_;
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        uint16_t year = 1900 + t->tm_year;
        w.data[0] = year; w.data[1] = year >> 8;
        w.data[2] = t->tm_mon + 1;
        w.data[3] = t->tm_mday;
        w.data[4] = t->tm_hour;
        w.data[5] = t->tm_min;
        w.data[6] = t->tm_sec;
        w.data[7] = t->tm_wday == 0 ? 7 : t->tm_wday;
        w.data[8] = 0;    // Fractions256
        w.data[9] = 0x01; // Adjust: manual
        w.len = 10;
        fprintf(stderr, "[Companion] Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                year, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    }

    void add_write_str(uint16_t h, const char* s) {
        auto& w = writes_[num_writes_++];
        w.handle = h;
        w.len = strlen(s);
        if (w.len > 200) w.len = 200;
        memcpy(w.data, s, w.len);
    }

    void add_write_byte(uint16_t h, uint8_t val) {
        auto& w = writes_[num_writes_++];
        w.handle = h;
        w.data[0] = val;
        w.len = 1;
    }

    void add_write_notification() {
        auto& w = writes_[num_writes_++];
        w.handle = h_alert_;
        // ANS New Alert: CategoryID(1) + NumAlerts(1) + Text
        w.data[0] = 0x00; // Simple Alert
        w.data[1] = 0x01; // 1 alert
        const char* msg = "Emulator ready";
        uint8_t mlen = strlen(msg);
        memcpy(w.data + 2, msg, mlen);
        w.len = 2 + mlen;
    }

    void add_write_weather() {
        auto& w = writes_[num_writes_++];
        w.handle = h_weather_;
        // SimpleWeatherService: msgType(1) + timestamp(8) + temp(2) + min(2) + max(2) + icon(1) + location
        w.data[0] = 0; // CurrentWeather
        // Timestamp (uint64_t LE, UNIX epoch)
        uint64_t ts = (uint64_t)time(nullptr);
        memcpy(w.data + 1, &ts, 8);
        // Current temp: 21.5°C = 2150 in 0.01°C
        int16_t temp = 2150;
        memcpy(w.data + 9, &temp, 2);
        // Min: 18°C
        int16_t tmin = 1800;
        memcpy(w.data + 11, &tmin, 2);
        // Max: 25°C
        int16_t tmax = 2500;
        memcpy(w.data + 13, &tmax, 2);
        // Icon: 1 = Few Clouds
        w.data[15] = 1;
        // Location
        const char* loc = "Berlin";
        uint8_t llen = strlen(loc);
        memcpy(w.data + 16, loc, llen);
        w.len = 16 + llen;
    }
};

} // namespace pt
