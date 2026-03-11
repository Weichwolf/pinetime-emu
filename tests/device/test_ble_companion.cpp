#include "../../src/device/ble_companion.hpp"
#include <cstdio>
#include <cstring>

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); } \
    else { tests_passed++; } \
} while(0)

// ================================================================
// ATT response builders (simulating device replies)
// ================================================================

// Build ATT Exchange MTU Response (opcode 0x03)
static void build_mtu_response(uint8_t* out, size_t* len, uint16_t mtu) {
    out[0] = 0x03;
    out[1] = mtu; out[2] = mtu >> 8;
    *len = 3;
}

// Build ATT Exchange MTU Request (from device, opcode 0x02)
static void build_mtu_request(uint8_t* out, size_t* len, uint16_t mtu) {
    out[0] = 0x02;
    out[1] = mtu; out[2] = mtu >> 8;
    *len = 3;
}

// Build ATT Read By Group Type Response (opcode 0x11)
// 16-bit UUID services
static void build_svc_disc_16(uint8_t* out, size_t* len,
                               uint16_t start, uint16_t end, uint16_t uuid) {
    out[0] = 0x11; // Read By Group Type Response
    out[1] = 6;    // entry_len = 6 (start(2) + end(2) + uuid16(2))
    out[2] = start; out[3] = start >> 8;
    out[4] = end; out[5] = end >> 8;
    out[6] = uuid; out[7] = uuid >> 8;
    *len = 8;
}

// Build ATT Read By Group Type Response with 128-bit UUID
static void build_svc_disc_128(uint8_t* out, size_t* len,
                                uint16_t start, uint16_t end,
                                uint16_t sid) {
    out[0] = 0x11;
    out[1] = 20; // entry_len = 20 (start(2) + end(2) + uuid128(16))
    out[2] = start; out[3] = start >> 8;
    out[4] = end; out[5] = end >> 8;
    // UUID LE: base(12) + char_id(2) + svc_id(2)
    // Base: d0 42 19 3a 3b 43 23 8e fe 48 fc 78
    uint8_t base[12] = {0xd0,0x42,0x19,0x3a,0x3b,0x43,0x23,0x8e,0xfe,0x48,0xfc,0x78};
    memcpy(out + 6, base, 12);
    out[18] = 0x00; out[19] = 0x00; // char_id = 0
    out[20] = sid; out[21] = sid >> 8;
    *len = 22;
}

// Build ATT Error Response (opcode 0x01)
static void build_error(uint8_t* out, size_t* len,
                         uint8_t req_opcode, uint16_t handle, uint8_t error) {
    out[0] = 0x01;
    out[1] = req_opcode;
    out[2] = handle; out[3] = handle >> 8;
    out[4] = error;
    *len = 5;
}

// Build ATT Read By Type Response (opcode 0x09) for 16-bit UUID char
static void build_char_disc_16(uint8_t* out, size_t* len,
                                uint16_t handle, uint8_t props,
                                uint16_t val_handle, uint16_t uuid) {
    out[0] = 0x09;
    out[1] = 7; // entry_len = 7 (handle(2) + props(1) + val_handle(2) + uuid16(2))
    out[2] = handle; out[3] = handle >> 8;
    out[4] = props;
    out[5] = val_handle; out[6] = val_handle >> 8;
    out[7] = uuid; out[8] = uuid >> 8;
    *len = 9;
}

// Build ATT Read By Type Response for 128-bit UUID char
static void build_char_disc_128(uint8_t* out, size_t* len,
                                 uint16_t handle, uint8_t props,
                                 uint16_t val_handle,
                                 uint16_t cid, uint16_t sid) {
    out[0] = 0x09;
    out[1] = 21; // handle(2) + props(1) + val_handle(2) + uuid128(16)
    out[2] = handle; out[3] = handle >> 8;
    out[4] = props;
    out[5] = val_handle; out[6] = val_handle >> 8;
    uint8_t base[12] = {0xd0,0x42,0x19,0x3a,0x3b,0x43,0x23,0x8e,0xfe,0x48,0xfc,0x78};
    memcpy(out + 7, base, 12);
    out[19] = cid; out[20] = cid >> 8;
    out[21] = sid; out[22] = sid >> 8;
    *len = 23;
}

// Build ATT Write Response (opcode 0x13)
static void build_write_response(uint8_t* out, size_t* len) {
    out[0] = 0x13;
    *len = 1;
}

// ================================================================
// Tests
// ================================================================

static void test_initial_state() {
    pt::BleCompanion comp;
    CHECK(!comp.is_done(), "Initial: not done");
    CHECK(!comp.has_pending(), "Initial: no pending");
}

static void test_mtu_exchange_response() {
    pt::BleCompanion comp;
    comp.on_connected();
    CHECK(comp.has_pending(), "After connect: has MTU request pending");

    // Get the MTU request
    uint8_t buf[256];
    size_t n0 = comp.get_pending(buf);
    CHECK(n0 == 3, "MTU request length");
    CHECK(buf[0] == 0x02, "MTU request opcode");

    // Feed MTU response
    uint8_t rsp[3];
    size_t rsp_len;
    build_mtu_response(rsp, &rsp_len, 185);
    comp.on_att_received(rsp, rsp_len);

    // Should now have service discovery request
    CHECK(comp.has_pending(), "After MTU: has svc disc pending");
    comp.get_pending(buf);
    CHECK(buf[0] == 0x10, "Svc disc request opcode (Read By Group Type)");
}

static void test_device_initiated_mtu() {
    pt::BleCompanion comp;
    comp.on_connected();

    // Drain our MTU request
    uint8_t buf[256];
    comp.get_pending(buf);

    // Device sends MTU Request instead of responding
    uint8_t req[3];
    size_t req_len;
    build_mtu_request(req, &req_len, 247);
    comp.on_att_received(req, req_len);

    // Should respond with MTU Response
    CHECK(comp.has_pending(), "Device MTU req: has response");
    comp.get_pending(buf);
    CHECK(buf[0] == 0x03, "Device MTU req: response opcode");

    // After response is consumed, should start svc discovery
    CHECK(comp.has_pending(), "Device MTU req: then svc disc");
    comp.get_pending(buf);
    CHECK(buf[0] == 0x10, "Device MTU req: svc disc opcode");
}

// Full companion flow: MTU → service disc → char disc → writes → done
static void test_full_companion_flow() {
    pt::BleCompanion comp;
    comp.on_connected();

    uint8_t buf[256], rsp[256];
    size_t rsp_len;

    // 1. MTU exchange
    comp.get_pending(buf); // Our MTU request
    build_mtu_response(rsp, &rsp_len, 185);
    comp.on_att_received(rsp, rsp_len);

    // 2. Service discovery — send CTS (0x1805)
    comp.get_pending(buf); // Svc disc request
    CHECK(buf[0] == 0x10, "Flow: svc disc req");

    build_svc_disc_16(rsp, &rsp_len, 0x0010, 0x0015, 0x1805); // CTS
    comp.on_att_received(rsp, rsp_len);

    // Continues discovery...
    comp.get_pending(buf);
    CHECK(buf[0] == 0x10, "Flow: svc disc continues");

    // Send ANS (0x1811)
    build_svc_disc_16(rsp, &rsp_len, 0x0020, 0x0025, 0x1811);
    comp.on_att_received(rsp, rsp_len);

    comp.get_pending(buf);
    // Send Music service (custom UUID, sid=0x0000)
    build_svc_disc_128(rsp, &rsp_len, 0x0030, 0x003F, 0x0000);
    comp.on_att_received(rsp, rsp_len);

    comp.get_pending(buf);
    // Send Weather service (custom UUID, sid=0x0004)
    build_svc_disc_128(rsp, &rsp_len, 0x0040, 0x004F, 0x0004);
    comp.on_att_received(rsp, rsp_len);

    comp.get_pending(buf);
    // End of services: error (Attribute Not Found)
    build_error(rsp, &rsp_len, 0x10, 0xFFFF, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // 3. Characteristic discovery — CTS phase (char_phase_=0)
    comp.get_pending(buf);
    CHECK(buf[0] == 0x08, "Flow: char disc req (CTS)");

    // Return Current Time char (UUID 0x2A2B), val_handle=0x0012
    build_char_disc_16(rsp, &rsp_len, 0x0011, 0x0A, 0x0012, 0x2A2B);
    comp.on_att_received(rsp, rsp_len);

    comp.get_pending(buf);
    // End CTS chars
    build_error(rsp, &rsp_len, 0x08, 0x0015, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // Music phase (char_phase_=1)
    comp.get_pending(buf);
    CHECK(buf[0] == 0x08, "Flow: char disc req (Music)");

    // Return track char (cid=1, sid=0)
    build_char_disc_128(rsp, &rsp_len, 0x0031, 0x02, 0x0032, 1, 0);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);

    // Return artist char (cid=2, sid=0)
    build_char_disc_128(rsp, &rsp_len, 0x0033, 0x02, 0x0034, 2, 0);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);

    // Return album char (cid=3, sid=0)
    build_char_disc_128(rsp, &rsp_len, 0x0035, 0x02, 0x0036, 3, 0);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);

    // Return status char (cid=4, sid=0)
    build_char_disc_128(rsp, &rsp_len, 0x0037, 0x02, 0x0038, 4, 0);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);

    // End music chars
    build_error(rsp, &rsp_len, 0x08, 0x003F, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // ANS phase (char_phase_=2)
    comp.get_pending(buf);
    CHECK(buf[0] == 0x08, "Flow: char disc req (ANS)");

    // Return New Alert char (UUID 0x2A46), val_handle=0x0022
    build_char_disc_16(rsp, &rsp_len, 0x0021, 0x04, 0x0022, 0x2A46);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);

    // End ANS chars
    build_error(rsp, &rsp_len, 0x08, 0x0025, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // Weather phase (char_phase_=3)
    comp.get_pending(buf);
    CHECK(buf[0] == 0x08, "Flow: char disc req (Weather)");

    // Return weather char (cid=1, sid=4), val_handle=0x0042
    build_char_disc_128(rsp, &rsp_len, 0x0041, 0x02, 0x0042, 1, 4);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);

    // End weather chars
    build_error(rsp, &rsp_len, 0x08, 0x004F, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // 4. Write phase — should have pending writes
    CHECK(!comp.is_done(), "Flow: not done before writes");

    // Count and acknowledge writes
    int write_count = 0;
    while (comp.has_pending()) {
        comp.get_pending(buf);
        CHECK(buf[0] == 0x12, "Flow: write request opcode");
        write_count++;

        // Acknowledge write
        build_write_response(rsp, &rsp_len);
        comp.on_att_received(rsp, rsp_len);
    }

    CHECK(write_count == 7, "Flow: 7 writes (time, track, artist, album, status, alert, weather)");
    CHECK(comp.is_done(), "Flow: done after all writes");
}

static void test_no_services_found() {
    pt::BleCompanion comp;
    comp.on_connected();

    uint8_t buf[256], rsp[256];
    size_t rsp_len;

    // MTU
    comp.get_pending(buf);
    build_mtu_response(rsp, &rsp_len, 185);
    comp.on_att_received(rsp, rsp_len);

    // Service discovery: immediately error (no services)
    comp.get_pending(buf);
    build_error(rsp, &rsp_len, 0x10, 0x0001, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // No services → no chars → no writes → done
    CHECK(comp.is_done(), "No services: immediately done");
}

static void test_partial_services() {
    // Only CTS found, no others
    pt::BleCompanion comp;
    comp.on_connected();

    uint8_t buf[256], rsp[256];
    size_t rsp_len;

    // MTU
    comp.get_pending(buf);
    build_mtu_response(rsp, &rsp_len, 185);
    comp.on_att_received(rsp, rsp_len);

    // Service discovery: CTS only
    comp.get_pending(buf);
    build_svc_disc_16(rsp, &rsp_len, 0x0010, 0x0015, 0x1805);
    comp.on_att_received(rsp, rsp_len);

    comp.get_pending(buf);
    build_error(rsp, &rsp_len, 0x10, 0xFFFF, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // Char disc for CTS
    comp.get_pending(buf);
    build_char_disc_16(rsp, &rsp_len, 0x0011, 0x0A, 0x0012, 0x2A2B);
    comp.on_att_received(rsp, rsp_len);

    comp.get_pending(buf);
    build_error(rsp, &rsp_len, 0x08, 0x0015, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // Should only write time
    int writes = 0;
    while (comp.has_pending()) {
        comp.get_pending(buf);
        CHECK(buf[0] == 0x12, "Partial: write request");
        writes++;
        build_write_response(rsp, &rsp_len);
        comp.on_att_received(rsp, rsp_len);
    }
    CHECK(writes == 1, "Partial: only 1 write (time)");
    CHECK(comp.is_done(), "Partial: done");
}

static void test_time_write_format() {
    pt::BleCompanion comp;
    comp.on_connected();

    uint8_t buf[256], rsp[256];
    size_t rsp_len;

    // Quick path to writes: MTU → no services → done? No, need CTS.
    // MTU
    comp.get_pending(buf);
    build_mtu_response(rsp, &rsp_len, 185);
    comp.on_att_received(rsp, rsp_len);

    // CTS only
    comp.get_pending(buf);
    build_svc_disc_16(rsp, &rsp_len, 0x0010, 0x0015, 0x1805);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);
    build_error(rsp, &rsp_len, 0x10, 0xFFFF, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // Current Time char
    comp.get_pending(buf);
    build_char_disc_16(rsp, &rsp_len, 0x0011, 0x0A, 0x0012, 0x2A2B);
    comp.on_att_received(rsp, rsp_len);
    comp.get_pending(buf);
    build_error(rsp, &rsp_len, 0x08, 0x0015, 0x0A);
    comp.on_att_received(rsp, rsp_len);

    // Get the write
    CHECK(comp.has_pending(), "Time: has write");
    size_t n = comp.get_pending(buf);
    CHECK(buf[0] == 0x12, "Time: write request opcode");
    uint16_t handle = buf[1] | (buf[2] << 8);
    CHECK(handle == 0x0012, "Time: correct handle");
    // Data starts at buf[3], length = n - 3
    size_t data_len = n - 3;
    CHECK(data_len == 10, "Time: CTS data length = 10");
    // Year is 2 bytes LE
    uint16_t year = buf[3] | (buf[4] << 8);
    CHECK(year >= 2024 && year <= 2030, "Time: plausible year");
    // Month 1-12
    CHECK(buf[5] >= 1 && buf[5] <= 12, "Time: valid month");
    // Day 1-31
    CHECK(buf[6] >= 1 && buf[6] <= 31, "Time: valid day");
    // Hour 0-23
    CHECK(buf[7] <= 23, "Time: valid hour");
    // Min 0-59
    CHECK(buf[8] <= 59, "Time: valid minute");
    // Sec 0-59
    CHECK(buf[9] <= 59, "Time: valid second");
    // DoW 1-7
    CHECK(buf[10] >= 1 && buf[10] <= 7, "Time: valid day of week");
}

int main() {
    test_initial_state();
    test_mtu_exchange_response();
    test_device_initiated_mtu();
    test_full_companion_flow();
    test_no_services_found();
    test_partial_services();
    test_time_write_format();

    fprintf(stderr, "[test_ble_companion] %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
