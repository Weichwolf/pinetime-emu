#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace pt {

// ST7789 LCD Controller (240x320 internal, 240x240 visible on PineTime)
// PineTime maps to rows 80-319 (Y offset = 80).
// Connected via SPI. DC pin selects command(0) vs data(1).
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

    // Framebuffer: 240x320 (full controller range)
    static constexpr int FB_W = 240;
    static constexpr int FB_H = 320;
    std::array<uint16_t, FB_W * FB_H> fb_{};

    // Data/Command pin state
    bool dc_pin_ = false;

public:
    static constexpr int WIDTH = 240;
    static constexpr int HEIGHT = 240;
    static constexpr int ROW_OFFSET = 0; // InfiniTime addresses rows 0-239 directly

    // Returns pointer to visible area (row 80 onward)
    const uint16_t* framebuffer() const { return fb_.data() + ROW_OFFSET * FB_W; }
    bool is_on() const { return display_on_ && !sleep_; }
    uint8_t last_cmd() const { return cmd_; }

    void set_dc(bool data_mode) { dc_pin_ = data_mode; }

    // Diagnostics
    uint32_t cmd_count_ = 0;
    uint32_t data_bytes_ = 0;
    uint32_t ramwr_count_ = 0; // separate RAMWR starts
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
            // Log first few RAMWR calls and every 240th (full frame boundary)
            if (ramwr_count_ <= 3 || ramwr_count_ == 240 || ramwr_count_ == 241 ||
                ramwr_count_ == 480 || ramwr_count_ == 481 ||
                ramwr_count_ == 720 || ramwr_count_ == 721 ||
                ramwr_count_ == 8800 || ramwr_count_ == 9040)
                fprintf(stderr, "[LCD] RAMWR #%u row=%d-%d col=%d-%d\n",
                        ramwr_count_, row_start_, row_end_, col_start_, col_end_);
            state_ = IDLE;
            break;
        case 0x36: state_ = READING_ARGS; break; // MADCTL
        case 0x3A: state_ = READING_ARGS; break; // COLMOD
        case 0x00: break;
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
        case 0x2A:
            if (arg_idx_ >= 4) {
                col_start_ = (args_[0] << 8) | args_[1];
                col_end_ = (args_[2] << 8) | args_[3];
                state_ = IDLE;
            }
            break;
        case 0x2B:
            if (arg_idx_ >= 4) {
                row_start_ = (args_[0] << 8) | args_[1];
                row_end_ = (args_[2] << 8) | args_[3];
                state_ = IDLE;
            }
            break;
        case 0x36:
            if (arg_idx_ >= 1) { madctl_ = args_[0]; state_ = IDLE; }
            break;
        case 0x3A:
            if (arg_idx_ >= 1) { colmod_ = args_[0]; state_ = IDLE; }
            break;
        }
    }

    void write_pixel(uint16_t color) {
        if (write_row_ < FB_H && write_col_ < FB_W) {
            fb_[write_row_ * FB_W + write_col_] = color;
            pixels_written_++;
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
};

} // namespace pt
