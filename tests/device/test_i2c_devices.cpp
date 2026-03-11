#include "../test.hpp"
#include "device/i2c_devices.hpp"

using namespace pt;

// --- CST816S ---

void test_cst_chip_id() {
    CST816S cst;
    uint8_t reg = 0xA7;
    cst.i2c_write(&reg, 1);
    uint8_t id;
    cst.i2c_read(&id, 1);
    ASSERT_EQ(id, 0xB4);
}

void test_cst_touch() {
    CST816S cst;
    cst.set_touch(true, 120, 80, 0);

    uint8_t reg = 0x02;
    cst.i2c_write(&reg, 1);
    uint8_t data[5];
    cst.i2c_read(data, 5);

    ASSERT_EQ(data[0], 1u); // FingerNum=1
    uint8_t action = (data[1] >> 6) & 3;
    ASSERT_EQ(action, 0u); // Down
    uint16_t x = ((data[1] & 0x0F) << 8) | data[2];
    uint16_t y = ((data[3] & 0x0F) << 8) | data[4];
    ASSERT_EQ(x, 120u);
    ASSERT_EQ(y, 80u);
}

void test_cst_release() {
    CST816S cst;
    cst.set_touch(false, 0, 0, 0);

    uint8_t reg = 0x02;
    cst.i2c_write(&reg, 1);
    uint8_t data[2];
    cst.i2c_read(data, 2);

    ASSERT_EQ(data[0], 0u); // FingerNum=0
    uint8_t action = (data[1] >> 6) & 3;
    ASSERT_EQ(action, 1u); // Up
}

void test_cst_gesture() {
    CST816S cst;
    cst.set_touch(true, 0, 0, 0x01); // Slide Up

    uint8_t reg = 0x01;
    cst.i2c_write(&reg, 1);
    uint8_t gesture;
    cst.i2c_read(&gesture, 1);
    ASSERT_EQ(gesture, 0x01);
}

void test_cst_irq_callback() {
    CST816S cst;
    bool irq_called = false;
    bool irq_val = false;
    cst.set_irq_callback([&](bool touched) {
        irq_called = true;
        irq_val = touched;
    });
    cst.set_touch(true, 100, 100);
    ASSERT(irq_called);
    ASSERT(irq_val);
}

// --- HRS3300 ---

void test_hrs_id() {
    HRS3300 hrs;
    uint8_t reg = 0x00;
    hrs.i2c_write(&reg, 1);
    uint8_t id;
    hrs.i2c_read(&id, 1);
    ASSERT_EQ(id, 0x21);
}

void test_hrs_register_write() {
    HRS3300 hrs;
    uint8_t data[] = {0x01, 0x80}; // Enable register = 0x80
    hrs.i2c_write(data, 2);
    ASSERT_EQ(hrs.reg(0x01), 0x80);
}

void test_hrs_ppg_with_cycles() {
    HRS3300 hrs;
    uint64_t cycles = 64000000; // 1 second
    hrs.set_cycles(&cycles);

    uint8_t reg = 0x09;
    hrs.i2c_write(&reg, 1);
    uint8_t data[7];
    hrs.i2c_read(data, 7);

    // PPG encoding: m=reg[0x09], h=reg[0x0A], l=reg[0x0F]
    // Read at offset 0x09: data[0]=reg[9], data[1]=reg[A], ..., data[6]=reg[F]
    uint8_t m = data[0]; // reg[0x09]
    uint8_t h = data[1]; // reg[0x0A]
    uint8_t l = data[6]; // reg[0x0F]
    uint32_t ppg = (m << 8) | ((h & 0xF) << 4) | (l & 0xF) | ((l & 0x30) << 12);
    ASSERT(ppg > 0);
    ASSERT(ppg < 262144); // 18-bit max
}

void test_hrs_read_write_counts() {
    HRS3300 hrs;
    ASSERT_EQ(hrs.read_count_, 0u);
    uint8_t reg = 0;
    hrs.i2c_write(&reg, 1);
    uint8_t buf;
    hrs.i2c_read(&buf, 1);
    ASSERT_EQ(hrs.read_count_, 1u);
}

// --- BMA4xx ---

void test_bma_chip_id() {
    BMA4xx bma;
    uint8_t reg = 0x00;
    bma.i2c_write(&reg, 1);
    uint8_t id;
    bma.i2c_read(&id, 1);
    ASSERT_EQ(id, 0x11);
}

void test_bma_internal_status() {
    BMA4xx bma;
    uint8_t reg = 0x2A;
    bma.i2c_write(&reg, 1);
    uint8_t status;
    bma.i2c_read(&status, 1);
    ASSERT_EQ(status, 0x01); // Init OK
}

void test_bma_accel_default() {
    BMA4xx bma;
    uint8_t reg = 0x16;
    bma.i2c_write(&reg, 1);
    uint8_t data[2];
    bma.i2c_read(data, 2);
    int16_t z = data[0] | (data[1] << 8);
    ASSERT(z > 0); // Z ~4096 (1g)
}

void test_bma_set_accel() {
    BMA4xx bma;
    bma.set_accel(100, -200, 4096);
    uint8_t reg = 0x12;
    bma.i2c_write(&reg, 1);
    uint8_t data[6];
    bma.i2c_read(data, 6);
    int16_t x = data[0] | (data[1] << 8);
    int16_t y = data[2] | (data[3] << 8);
    int16_t z = data[4] | (data[5] << 8);
    ASSERT_EQ(x, 100);
    ASSERT_EQ((uint16_t)y, (uint16_t)-200);
    ASSERT_EQ(z, 4096);
}

void test_bma_step_counter() {
    BMA4xx bma;
    uint64_t cycles = 64000000ULL * 60; // 60 seconds
    bma.set_cycles(&cycles);

    uint8_t reg = 0x1E;
    bma.i2c_write(&reg, 1);
    uint8_t data[4];
    bma.i2c_read(data, 4);
    uint32_t steps = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    // ~100 steps/min -> 60 seconds -> ~100 steps
    ASSERT(steps >= 90);
    ASSERT(steps <= 110);
}

void test_bma_register_write() {
    BMA4xx bma;
    uint8_t data[] = {0x40, 0x01}; // Write 0x01 to register 0x40
    bma.i2c_write(data, 2);
    uint8_t reg = 0x40;
    bma.i2c_write(&reg, 1);
    uint8_t val;
    bma.i2c_read(&val, 1);
    ASSERT_EQ(val, 0x01);
}

// --- CST816S extended ---

void test_cst_all_gestures() {
    CST816S cst;
    uint8_t gestures[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05}; // None, Up, Down, Left, Right, ...
    for (auto g : gestures) {
        cst.set_touch(true, 50, 50, g);
        uint8_t reg = 0x01;
        cst.i2c_write(&reg, 1);
        uint8_t val;
        cst.i2c_read(&val, 1);
        ASSERT_EQ(val, g);
    }
}

void test_cst_touch_coordinates_extremes() {
    CST816S cst;
    // Max coordinates: 239,239
    cst.set_touch(true, 239, 239, 0);
    uint8_t reg = 0x03;
    cst.i2c_write(&reg, 1);
    uint8_t data[4];
    cst.i2c_read(data, 4);
    uint16_t x = ((data[0] & 0x0F) << 8) | data[1];
    uint16_t y = ((data[2] & 0x0F) << 8) | data[3];
    ASSERT_EQ(x, 239u);
    ASSERT_EQ(y, 239u);
}

void test_cst_irq_release() {
    CST816S cst;
    bool last_touched = true;
    cst.set_irq_callback([&](bool t) { last_touched = t; });
    cst.set_touch(true, 100, 100);
    ASSERT(last_touched);
    cst.set_touch(false, 0, 0);
    ASSERT(!last_touched);
}

void test_cst_sequential_touches() {
    CST816S cst;
    int irq_count = 0;
    cst.set_irq_callback([&](bool) { irq_count++; });

    cst.set_touch(true, 10, 10);
    cst.set_touch(false, 0, 0);
    cst.set_touch(true, 200, 200);
    cst.set_touch(false, 0, 0);
    ASSERT_EQ(irq_count, 4);
}

// --- HRS3300 extended ---

void test_hrs_ppg_periodicity() {
    HRS3300 hrs;
    // Read PPG at two timepoints 1 cardiac cycle apart (72 BPM = 833ms)
    uint64_t cy1 = 64000000ULL; // 1.0s
    uint64_t cy2 = cy1 + (uint64_t)(64e6 * 60.0 / 72.0); // +1 beat period
    hrs.set_cycles(&cy1);

    uint8_t reg = 0x09;
    hrs.i2c_write(&reg, 1);
    uint8_t d1[7];
    hrs.i2c_read(d1, 7);

    hrs.set_cycles(&cy2);
    hrs.i2c_write(&reg, 1);
    uint8_t d2[7];
    hrs.i2c_read(d2, 7);

    // PPG values at the same phase should be similar (within noise)
    uint32_t ppg1 = (d1[0] << 8) | ((d1[1] & 0xF) << 4) | (d1[6] & 0xF) | ((d1[6] & 0x30) << 12);
    uint32_t ppg2 = (d2[0] << 8) | ((d2[1] & 0xF) << 4) | (d2[6] & 0xF) | ((d2[6] & 0x30) << 12);

    // Should be within ~5% (noise + floating point)
    int32_t diff = (int32_t)ppg1 - (int32_t)ppg2;
    if (diff < 0) diff = -diff;
    ASSERT(diff < 5000); // Reasonable tolerance for same-phase values
}

void test_hrs_als_values() {
    HRS3300 hrs;
    uint64_t cycles = 64000000ULL;
    hrs.set_cycles(&cycles);

    // ALS registers: m=0x08, h=0x0D, l=0x0E
    uint8_t reg = 0x08;
    hrs.i2c_write(&reg, 1);
    uint8_t m;
    hrs.i2c_read(&m, 1);
    // m should be 0x0C per code
    ASSERT_EQ(m, 0x0C);

    reg = 0x0D;
    hrs.i2c_write(&reg, 1);
    uint8_t h;
    hrs.i2c_read(&h, 1);
    ASSERT_EQ(h, 0x00);

    reg = 0x0E;
    hrs.i2c_write(&reg, 1);
    uint8_t l;
    hrs.i2c_read(&l, 1);
    ASSERT_EQ(l, 0x04);
}

void test_hrs_enable_disable() {
    HRS3300 hrs;
    // Enable HR sensor
    uint8_t enable[] = {0x01, 0x80};
    hrs.i2c_write(enable, 2);
    ASSERT_EQ(hrs.reg(0x01), 0x80);

    // Disable
    uint8_t disable[] = {0x01, 0x00};
    hrs.i2c_write(disable, 2);
    ASSERT_EQ(hrs.reg(0x01), 0x00);
}

void test_hrs_write_count() {
    HRS3300 hrs;
    ASSERT_EQ(hrs.write_count_, 0u);
    uint8_t data[] = {0x01, 0x80};
    hrs.i2c_write(data, 2);
    ASSERT_EQ(hrs.write_count_, 1u);
}

// --- BMA4xx extended ---

void test_bma_walking_motion() {
    BMA4xx bma;
    uint64_t cycles = 64000000ULL * 2; // 2 seconds
    bma.set_cycles(&cycles);

    uint8_t reg = 0x12;
    bma.i2c_write(&reg, 1);
    uint8_t data[6];
    bma.i2c_read(data, 6);
    int16_t x = data[0] | (data[1] << 8);
    int16_t y = data[2] | (data[3] << 8);
    int16_t z = data[4] | (data[5] << 8);

    // Z should be ~4096 (1g) ± bounce
    ASSERT(z > 2000);
    ASSERT(z < 6000);
    // X and Y should be small (lateral sway)
    ASSERT(x > -1000 && x < 1000);
    ASSERT(y > -1000 && y < 1000);
}

void test_bma_step_counter_grows() {
    BMA4xx bma;
    uint64_t cy1 = 64000000ULL * 30; // 30s
    uint64_t cy2 = 64000000ULL * 60; // 60s
    bma.set_cycles(&cy1);
    uint8_t reg = 0x1E;
    bma.i2c_write(&reg, 1);
    uint8_t d1[4];
    bma.i2c_read(d1, 4);
    uint32_t s1 = d1[0] | (d1[1] << 8) | (d1[2] << 16) | (d1[3] << 24);

    bma.set_cycles(&cy2);
    bma.i2c_write(&reg, 1);
    uint8_t d2[4];
    bma.i2c_read(d2, 4);
    uint32_t s2 = d2[0] | (d2[1] << 8) | (d2[2] << 16) | (d2[3] << 24);

    ASSERT(s2 > s1); // Steps grow over time
    ASSERT(s2 - s1 >= 40); // ~50 steps in 30s
    ASSERT(s2 - s1 <= 60);
}

void test_bma_config_registers() {
    BMA4xx bma;
    // ACCEL_CONF at 0x40
    uint8_t data[] = {0x40, 0xA8}; // ODR=100Hz, BWP=normal
    bma.i2c_write(data, 2);
    uint8_t reg = 0x40;
    bma.i2c_write(&reg, 1);
    uint8_t val;
    bma.i2c_read(&val, 1);
    ASSERT_EQ(val, 0xA8);

    // ACCEL_RANGE at 0x41
    uint8_t range[] = {0x41, 0x01}; // ±4g
    bma.i2c_write(range, 2);
    reg = 0x41;
    bma.i2c_write(&reg, 1);
    bma.i2c_read(&val, 1);
    ASSERT_EQ(val, 0x01);
}

void test_bma_err_reg() {
    BMA4xx bma;
    uint8_t reg = 0x02;
    bma.i2c_write(&reg, 1);
    uint8_t err;
    bma.i2c_read(&err, 1);
    ASSERT_EQ(err, 0x00); // No error
}

void test_bma_status_drdy() {
    BMA4xx bma;
    uint8_t reg = 0x03;
    bma.i2c_write(&reg, 1);
    uint8_t status;
    bma.i2c_read(&status, 1);
    ASSERT(status & 0x01); // Data Ready
}

int main() {
    RUN(test_cst_chip_id);
    RUN(test_cst_touch);
    RUN(test_cst_release);
    RUN(test_cst_gesture);
    RUN(test_cst_irq_callback);
    RUN(test_cst_all_gestures);
    RUN(test_cst_touch_coordinates_extremes);
    RUN(test_cst_irq_release);
    RUN(test_cst_sequential_touches);
    RUN(test_hrs_id);
    RUN(test_hrs_register_write);
    RUN(test_hrs_ppg_with_cycles);
    RUN(test_hrs_read_write_counts);
    RUN(test_hrs_ppg_periodicity);
    RUN(test_hrs_als_values);
    RUN(test_hrs_enable_disable);
    RUN(test_hrs_write_count);
    RUN(test_bma_chip_id);
    RUN(test_bma_internal_status);
    RUN(test_bma_accel_default);
    RUN(test_bma_set_accel);
    RUN(test_bma_step_counter);
    RUN(test_bma_register_write);
    RUN(test_bma_walking_motion);
    RUN(test_bma_step_counter_grows);
    RUN(test_bma_config_registers);
    RUN(test_bma_err_reg);
    RUN(test_bma_status_drdy);
    TEST_REPORT();
}
