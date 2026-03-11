#include "../test.hpp"
#include "device/twim.hpp"
#include "device/memory.hpp"
#include "bus.hpp"

using namespace pt;

struct MockI2C : I2CDevice {
    uint8_t last_write[16]{};
    uint32_t last_write_len = 0;
    uint8_t read_data[16]{};
    uint32_t read_data_len = 0;

    void i2c_write(const uint8_t* data, uint32_t len) override {
        last_write_len = len > 16 ? 16 : len;
        memcpy(last_write, data, last_write_len);
    }
    void i2c_read(uint8_t* data, uint32_t len) override {
        for (uint32_t i = 0; i < len; i++)
            data[i] = (i < read_data_len) ? read_data[i] : 0xFF;
    }
};

void test_initial_state() {
    TWIM twim;
    ASSERT_EQ(twim.read(0x500, Width::Word), 0u); // ENABLE
}

void test_address() {
    TWIM twim;
    twim.write(0x588, Width::Word, 0x44);
    ASSERT_EQ(twim.read(0x588, Width::Word), 0x44u);
    twim.write(0x588, Width::Word, 0xFF); // Mask to 7 bits
    ASSERT_EQ(twim.read(0x588, Width::Word), 0x7Fu);
}

void test_tx() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    MockI2C dev;
    TWIM twim;
    twim.set_bus(&bus);
    twim.attach(0x15, &dev);

    bus.write8(0x20000000, 0xA7);
    bus.write8(0x20000001, 0x01);

    twim.write(0x588, Width::Word, 0x15); // ADDRESS
    twim.write(0x544, Width::Word, 0x20000000); // TXD.PTR
    twim.write(0x548, Width::Word, 2);          // TXD.MAXCNT
    twim.write(0x008, Width::Word, 1);          // STARTTX

    ASSERT_EQ(twim.read(0x150, Width::Word), 1u); // TXSTARTED
    ASSERT_EQ(twim.read(0x160, Width::Word), 1u); // LASTTX
    ASSERT_EQ(twim.read(0x54C, Width::Word), 2u); // TXD.AMOUNT
    ASSERT_EQ(dev.last_write_len, 2u);
    ASSERT_EQ(dev.last_write[0], 0xA7);
}

void test_rx() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());

    MockI2C dev;
    dev.read_data[0] = 0xB4;
    dev.read_data[1] = 0x42;
    dev.read_data_len = 2;

    TWIM twim;
    twim.set_bus(&bus);
    twim.attach(0x15, &dev);

    twim.write(0x588, Width::Word, 0x15);
    twim.write(0x534, Width::Word, 0x20000000); // RXD.PTR
    twim.write(0x538, Width::Word, 2);          // RXD.MAXCNT
    twim.write(0x000, Width::Word, 1);          // STARTRX

    ASSERT_EQ(twim.read(0x14C, Width::Word), 1u); // RXSTARTED
    ASSERT_EQ(twim.read(0x15C, Width::Word), 1u); // LASTRX
    ASSERT_EQ(twim.read(0x53C, Width::Word), 2u); // RXD.AMOUNT
    ASSERT_EQ(bus.read8(0x20000000), 0xB4);
    ASSERT_EQ(bus.read8(0x20000001), 0x42);
}

void test_shorts_lasttx_stop() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    MockI2C dev;
    TWIM twim;
    twim.set_bus(&bus);
    twim.attach(0x15, &dev);

    twim.write(0x200, Width::Word, 1u << 9); // LASTTX_STOP
    twim.write(0x588, Width::Word, 0x15);
    twim.write(0x544, Width::Word, 0x20000000);
    twim.write(0x548, Width::Word, 1);
    twim.write(0x008, Width::Word, 1);

    ASSERT_EQ(twim.read(0x104, Width::Word), 1u); // STOPPED
}

void test_shorts_lasttx_startrx() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    MockI2C dev;
    dev.read_data[0] = 0x99;
    dev.read_data_len = 1;
    TWIM twim;
    twim.set_bus(&bus);
    twim.attach(0x15, &dev);

    twim.write(0x200, Width::Word, 1u << 7); // LASTTX_STARTRX
    twim.write(0x588, Width::Word, 0x15);
    twim.write(0x544, Width::Word, 0x20000000);
    twim.write(0x548, Width::Word, 1);
    twim.write(0x534, Width::Word, 0x20000010);
    twim.write(0x538, Width::Word, 1);
    twim.write(0x008, Width::Word, 1);

    ASSERT_EQ(twim.read(0x14C, Width::Word), 1u); // RXSTARTED
    ASSERT_EQ(bus.read8(0x20000010), 0x99);
}

void test_stop() {
    TWIM twim;
    twim.write(0x014, Width::Word, 1);
    ASSERT_EQ(twim.read(0x104, Width::Word), 1u); // STOPPED
}

void test_errorsrc_clear() {
    TWIM twim;
    // ERRORSRC write-1-to-clear
    twim.write(0x4C4, Width::Word, 0xFF); // Shouldn't crash
}

void test_irq() {
    RAM ram(256);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    NVIC nvic;
    bool irq_pending = false;
    nvic.set_irq_pending_flag(&irq_pending);

    TWIM twim;
    twim.set_bus(&bus);
    twim.set_nvic(&nvic, 4);
    twim.write(0x304, Width::Word, 1u << 1); // INTENSET STOPPED
    twim.write(0x014, Width::Word, 1);        // STOP
    ASSERT(nvic.is_pending(4));
}

void test_inten() {
    TWIM twim;
    twim.write(0x304, Width::Word, 0xFF);
    ASSERT_EQ(twim.read(0x300, Width::Word), 0xFFu);
    twim.write(0x308, Width::Word, 0x0F);
    ASSERT_EQ(twim.read(0x300, Width::Word), 0xF0u);
}

int main() {
    RUN(test_initial_state);
    RUN(test_address);
    RUN(test_tx);
    RUN(test_rx);
    RUN(test_shorts_lasttx_stop);
    RUN(test_shorts_lasttx_startrx);
    RUN(test_stop);
    RUN(test_errorsrc_clear);
    RUN(test_irq);
    RUN(test_inten);
    TEST_REPORT();
}
