#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace pt {

// ST7789 LCD Controller (240x240, RGB565)
// Connected via SPI. DC pin selects command(0) vs data(1).
// This is NOT a bus Device — it's called from the SPIM callback.
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

    // Window
    uint16_t col_start_ = 0, col_end_ = 239;
    uint16_t row_start_ = 0, row_end_ = 239;
    uint16_t write_col_ = 0, write_row_ = 0;

    // Framebuffer: 240×240 RGB565 = 115200 bytes
    std::array<uint16_t, 240 * 240> fb_{};

    // Data/Command pin state (set by GPIO before SPI transfer)
    bool dc_pin_ = false; // false=command, true=data

public:
    static constexpr int WIDTH = 240;
    static constexpr int HEIGHT = 240;

    const uint16_t* framebuffer() const { return fb_.data(); }
    bool is_on() const { return display_on_ && !sleep_; }

    void set_dc(bool data_mode) { dc_pin_ = data_mode; }

    // Process SPI data (called from SPIM callback)
    void spi_transfer(const uint8_t* tx, uint32_t len) {
        if (!dc_pin_) {
            // Command mode
            for (uint32_t i = 0; i < len; i++)
                process_command(tx[i]);
        } else {
            // Data mode
            for (uint32_t i = 0; i < len; i++)
                process_data(tx[i]);
        }
    }

private:
    void process_command(uint8_t cmd) {
        // If previous command expected args, finalize
        cmd_ = cmd;
        arg_idx_ = 0;
        state_ = IDLE;

        switch (cmd) {
        case 0x01: // SWRESET
            sleep_ = true;
            display_on_ = false;
            col_start_ = 0; col_end_ = 239;
            row_start_ = 0; row_end_ = 239;
            break;
        case 0x11: sleep_ = false; break;   // SLPOUT
        case 0x10: sleep_ = true; break;    // SLPIN
        case 0x20: invert_ = false; break;  // INVOFF
        case 0x21: invert_ = true; break;   // INVON
        case 0x28: display_on_ = false; break; // DISPOFF
        case 0x29: display_on_ = true; break;  // DISPON
        case 0x2A: state_ = READING_ARGS; break; // CASET (column address set, 4 args)
        case 0x2B: state_ = READING_ARGS; break; // RASET (row address set, 4 args)
        case 0x2C: // RAMWR (memory write)
            write_col_ = col_start_;
            write_row_ = row_start_;
            state_ = IDLE; // Data will come as data-mode bytes
            break;
        case 0x36: state_ = READING_ARGS; break; // MADCTL (1 arg)
        case 0x3A: state_ = READING_ARGS; break; // COLMOD (1 arg)
        case 0x00: break; // NOP
        default: break;
        }
    }

    void process_data(uint8_t byte) {
        if (state_ == READING_ARGS) {
            args_[arg_idx_++] = byte;
            finalize_args();
            return;
        }

        // RAMWR data: RGB565, 2 bytes per pixel (big-endian from controller)
        if (cmd_ == 0x2C || cmd_ == 0x3C) {
            // Accumulate high/low byte
            if (arg_idx_ == 0) {
                args_[0] = byte; // High byte
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
        case 0x36: // MADCTL
            if (arg_idx_ >= 1) { madctl_ = args_[0]; state_ = IDLE; }
            break;
        case 0x3A: // COLMOD
            if (arg_idx_ >= 1) { colmod_ = args_[0]; state_ = IDLE; }
            break;
        }
    }

    void write_pixel(uint16_t color) {
        if (write_row_ < HEIGHT && write_col_ < WIDTH)
            fb_[write_row_ * WIDTH + write_col_] = color;

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
