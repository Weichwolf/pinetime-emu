#include "../test.hpp"
#include <cstring>
#include "device/ecb.hpp"
#include "device/memory.hpp"
#include "bus.hpp"

using namespace pt;

void test_registers() {
    ECB ecb;
    ecb.write(0x504, Width::Word, 0x20000100);
    ASSERT_EQ(ecb.read(0x504, Width::Word), 0x20000100u);
}

void test_inten() {
    ECB ecb;
    ecb.write(0x304, Width::Word, 0x3);
    ASSERT_EQ(ecb.read(0x300, Width::Word), 0x3u);
    ecb.write(0x308, Width::Word, 0x1);
    ASSERT_EQ(ecb.read(0x300, Width::Word), 0x2u);
}

void test_aes_encrypt() {
    RAM ram(1024);
    Bus bus;
    bus.set_fast_path(nullptr, 0, ram.data(), 0x20000000, ram.size());
    ECB ecb;
    ecb.set_bus(&bus);

    // NIST AES-128 test vector
    // Key: 2b7e151628aed2a6abf7158809cf4f3c
    // Plaintext: 6bc1bee22e409f96e93d7e117393172a
    // Ciphertext: 3ad77bb40d7a3660a89ecaf32466ef97
    uint8_t key[] = {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
                     0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    uint8_t plain[] = {0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
                       0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a};
    uint8_t expected[] = {0x3a,0xd7,0x7b,0xb4,0x0d,0x7a,0x36,0x60,
                          0xa8,0x9e,0xca,0xf3,0x24,0x66,0xef,0x97};

    uint32_t ptr = 0x100; // offset in RAM
    for (int i = 0; i < 16; i++) bus.write8(0x20000000 + ptr + i, key[i]);
    for (int i = 0; i < 16; i++) bus.write8(0x20000000 + ptr + 16 + i, plain[i]);

    ecb.write(0x504, Width::Word, 0x20000000 + ptr); // ECBDATAPTR
    ecb.write(0x000, Width::Word, 1);                 // STARTECB

    ASSERT_EQ(ecb.read(0x100, Width::Word), 1u); // ENDECB event

    for (int i = 0; i < 16; i++)
        ASSERT_EQ(bus.read8(0x20000000 + ptr + 32 + i), expected[i]);
}

void test_no_bus_error() {
    ECB ecb;
    ecb.write(0x504, Width::Word, 0x20000100);
    ecb.write(0x000, Width::Word, 1); // No bus -> ERRORECB
    ASSERT_EQ(ecb.read(0x104, Width::Word), 1u);
}

int main() {
    RUN(test_registers);
    RUN(test_inten);
    RUN(test_aes_encrypt);
    RUN(test_no_bus_error);
    TEST_REPORT();
}
