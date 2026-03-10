#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace pt {

// ST7789 LCD Controller (240x320 internal, 240x240 visible on PineTime)
// Connected via SPI. DC pin selects command(0) vs data(1).
// Supports vertical scrolling (VSCRDEF/VSCSAD) used by InfiniTime for screen transitions.
class ST7789 {
    enum State { IDLE, READING_ARGS };

    uint8_t cmd_ = 0;
    State state_ = IDLE;
    int arg_idx_ = 0;
    uint8_t args_[8]{};

    // Display state
    bool sleep_ = true;
    bool display_on_ = false;
    bool invert_ = false;
    uint8_t madctl_ = 0;
    uint8_t colmod_ = 0x05; // RGB565

    // Window (controller coordinates, 0-319 for rows)
    uint16_t col_start_ = 0, col_end_ = 239;
    uint16_t row_start_ = 0, row_end_ = 319;
    uint16_t write_col_ = 0, write_row_ = 0;

    // Vertical scrolling (VSCRDEF 0x33 + VSCSAD 0x37)
    uint16_t vsa_top_ = 0;       // Top Fixed Area (rows)
    uint16_t vsa_height_ = 320;  // Vertical Scrolling Area (rows)
    uint16_t vsa_bottom_ = 0;    // Bottom Fixed Area (rows)
    uint16_t vscroll_ = 0;       // Vertical Scroll Start Address

    // Framebuffer: 240x320 (full controller range)
    static constexpr int FB_W = 240;
    static constexpr int FB_H = 320;
    std::array<uint16_t, FB_W * FB_H> fb_{};

    // Composited output buffer (240x240 with scroll applied)
    mutable std::array<uint16_t, FB_W * 240> out_{};
    mutable bool out_dirty_ = true;

    // Data/Command pin state
    bool dc_pin_ = false;

public:
    static constexpr int WIDTH = 240;
    static constexpr int HEIGHT = 240;

    // Returns pointer to visible 240x240 area with vertical scroll applied
    const uint16_t* framebuffer() const {
        if (out_dirty_) {
            compose_output();
            out_dirty_ = false;
        }
        return out_.data();
    }
    bool is_on() const { return display_on_ && !sleep_; }

    void set_dc(bool data_mode) { dc_pin_ = data_mode; }

    // Diagnostics
    uint32_t cmd_count_ = 0;
    uint32_t data_bytes_ = 0;
    uint32_t ramwr_count_ = 0;
    uint32_t pixels_written_ = 0;
    uint32_t pixels_clipped_ = 0;
    uint32_t cmd_count() const { return cmd_count_; }
    uint32_t data_bytes() const { return data_bytes_; }

    void spi_transfer(const uint8_t* tx, uint32_t len) {
        if (!dc_pin_) {
            for (uint32_t i = 0; i < len; i++) {
                cmd_count_++;
                process_command(tx[i]);
            }
        } else {
            data_bytes_ += len;
            for (uint32_t i = 0; i < len; i++)
                process_data(tx[i]);
        }
    }

private:
    void process_command(uint8_t cmd) {
        cmd_ = cmd;
        arg_idx_ = 0;
        state_ = IDLE;

        switch (cmd) {
        case 0x01: // SWRESET
            sleep_ = true;
            display_on_ = false;
            col_start_ = 0; col_end_ = 239;
            row_start_ = 0; row_end_ = 319;
            vsa_top_ = 0; vsa_height_ = 320; vsa_bottom_ = 0;
            vscroll_ = 0;
            out_dirty_ = true;
            break;
        case 0x11: sleep_ = false; break;
        case 0x10: sleep_ = true; break;
        case 0x20: invert_ = false; break;
        case 0x21: invert_ = true; break;
        case 0x28: display_on_ = false; break;
        case 0x29: display_on_ = true; break;
        case 0x2A: state_ = READING_ARGS; break; // CASET
        case 0x2B: state_ = READING_ARGS; break; // RASET
        case 0x2C: // RAMWR
            write_col_ = col_start_;
            write_row_ = row_start_;
            ramwr_count_++;
            break;
        case 0x33: state_ = READING_ARGS; break; // VSCRDEF
        case 0x36: state_ = READING_ARGS; break; // MADCTL
        case 0x37: state_ = READING_ARGS; break; // VSCSAD
        case 0x3A: state_ = READING_ARGS; break; // COLMOD
        default: break;
        }
    }

    void process_data(uint8_t byte) {
        if (state_ == READING_ARGS) {
            args_[arg_idx_++] = byte;
            finalize_args();
            return;
        }

        if (cmd_ == 0x2C || cmd_ == 0x3C) {
            if (arg_idx_ == 0) {
                args_[0] = byte;
                arg_idx_ = 1;
            } else {
                uint16_t color = (args_[0] << 8) | byte;
                write_pixel(color);
                arg_idx_ = 0;
            }
        }
    }

    void finalize_args() {
        switch (cmd_) {
        case 0x2A: // CASET
            if (arg_idx_ >= 4) {
                col_start_ = (args_[0] << 8) | args_[1];
                col_end_ = (args_[2] << 8) | args_[3];
                state_ = IDLE;
            }
            break;
        case 0x2B: // RASET
            if (arg_idx_ >= 4) {
                row_start_ = (args_[0] << 8) | args_[1];
                row_end_ = (args_[2] << 8) | args_[3];
                state_ = IDLE;
            }
            break;
        case 0x33: // VSCRDEF: TFA[16] + VSA[16] + BFA[16] = 6 bytes
            if (arg_idx_ >= 6) {
                vsa_top_ = (args_[0] << 8) | args_[1];
                vsa_height_ = (args_[2] << 8) | args_[3];
                vsa_bottom_ = (args_[4] << 8) | args_[5];
                fprintf(stderr, "[LCD] VSCRDEF tfa=%d vsa=%d bfa=%d\n", vsa_top_, vsa_height_, vsa_bottom_);
                out_dirty_ = true;
                state_ = IDLE;
            }
            break;
        case 0x36: // MADCTL
            if (arg_idx_ >= 1) { madctl_ = args_[0]; state_ = IDLE; }
            break;
        case 0x37: // VSCSAD: 2 bytes
            if (arg_idx_ >= 2) {
                uint16_t old = vscroll_;
                vscroll_ = (args_[0] << 8) | args_[1];
                if (vscroll_ != old)
                    fprintf(stderr, "[LCD] VSCSAD %d -> %d\n", old, vscroll_);
                out_dirty_ = true;
                state_ = IDLE;
            }
            break;
        case 0x3A: // COLMOD
            if (arg_idx_ >= 1) { colmod_ = args_[0]; state_ = IDLE; }
            break;
        }
    }

    void write_pixel(uint16_t color) {
        if (write_row_ < FB_H && write_col_ < FB_W) {
            fb_[write_row_ * FB_W + write_col_] = color;
            pixels_written_++;
            out_dirty_ = true;
        } else {
            pixels_clipped_++;
        }

        write_col_++;
        if (write_col_ > col_end_) {
            write_col_ = col_start_;
            write_row_++;
            if (write_row_ > row_end_)
                write_row_ = row_start_;
        }
    }

    // Compose 240x240 output from 240x320 framebuffer with vertical scroll
    // ST7789 scroll layout: [TFA rows][VSA rows][BFA rows] = 320 total
    // VSCSAD selects which row of VSA appears at the top of the scroll area
    void compose_output() const {
        int out_row = 0;

        // Top Fixed Area: rows 0..vsa_top_-1 (no scroll)
        int tfa = vsa_top_;
        for (int i = 0; i < tfa && out_row < 240; i++, out_row++)
            memcpy(&out_[out_row * FB_W], &fb_[i * FB_W], FB_W * 2);

        // Vertical Scrolling Area: vsa_height_ rows starting at vsa_top_,
        // displayed starting from vscroll_ with wraparound
        int vsa_rows = vsa_height_;
        int vsa_start = vsa_top_; // first row of VSA in framebuffer
        int scroll_offset = vscroll_ - vsa_start; // offset within VSA
        if (scroll_offset < 0) scroll_offset += vsa_rows;

        int visible_vsa = 240 - tfa - vsa_bottom_;
        if (visible_vsa > vsa_rows) visible_vsa = vsa_rows;

        for (int i = 0; i < visible_vsa && out_row < 240; i++, out_row++) {
            int src_row = vsa_start + ((scroll_offset + i) % vsa_rows);
            memcpy(&out_[out_row * FB_W], &fb_[src_row * FB_W], FB_W * 2);
        }

        // Bottom Fixed Area: last vsa_bottom_ rows of the 320-row buffer
        int bfa_start = vsa_top_ + vsa_height_;
        for (int i = 0; i < (int)vsa_bottom_ && out_row < 240; i++, out_row++)
            memcpy(&out_[out_row * FB_W], &fb_[(bfa_start + i) * FB_W], FB_W * 2);

        // Fill remaining with black (shouldn't happen if TFA+VSA+BFA >= 240)
        while (out_row < 240) {
            memset(&out_[out_row * FB_W], 0, FB_W * 2);
            out_row++;
        }
    }
};

} // namespace pt
