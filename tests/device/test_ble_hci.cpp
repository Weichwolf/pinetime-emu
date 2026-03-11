#include "../../src/device/ble_hci.hpp"
#include "../../src/device/memory.hpp"
#include <cstdio>
#include <cstring>

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); } \
    else { tests_passed++; } \
} while(0)

// Helper: build H4 HCI command
static std::vector<uint8_t> hci_cmd(uint16_t opcode, const uint8_t* params = nullptr, uint8_t plen = 0) {
    std::vector<uint8_t> pkt;
    pkt.push_back(0x01); // H4_CMD
    pkt.push_back(opcode & 0xFF);
    pkt.push_back(opcode >> 8);
    pkt.push_back(plen);
    if (plen) pkt.insert(pkt.end(), params, params + plen);
    return pkt;
}

// Helper: parse Command Complete from tx_buffer
static bool parse_cc(const std::vector<uint8_t>& buf, size_t offset,
                     uint16_t* opcode, uint8_t* status, const uint8_t** params, uint8_t* plen) {
    if (offset + 3 > buf.size()) return false;
    if (buf[offset] != 0x04) return false; // H4_EVT
    if (buf[offset+1] != 0x0E) return false; // Command Complete
    uint8_t total_len = buf[offset+2];
    if (offset + 3 + total_len > buf.size()) return false;
    // buf[offset+3] = num_cmd_packets
    *opcode = buf[offset+4] | (buf[offset+5] << 8);
    *status = buf[offset+6];
    *params = &buf[offset+6];
    *plen = total_len - 3;
    return true;
}

static void test_hci_reset() {
    pt::BleHci hci;
    auto cmd = hci_cmd(0x0C03); // HCI_Reset
    hci.feed_hci(cmd.data(), cmd.size());

    auto& tx = hci.tx_buffer();
    uint16_t opcode; uint8_t status, plen; const uint8_t* params;
    CHECK(parse_cc(tx, 0, &opcode, &status, &params, &plen), "Reset: parse CC");
    CHECK(opcode == 0x0C03, "Reset: opcode match");
    CHECK(status == 0, "Reset: status OK");
}

static void test_read_local_version() {
    pt::BleHci hci;
    auto cmd = hci_cmd(0x1001);
    hci.feed_hci(cmd.data(), cmd.size());

    auto& tx = hci.tx_buffer();
    uint16_t opcode; uint8_t status, plen; const uint8_t* params;
    CHECK(parse_cc(tx, 0, &opcode, &status, &params, &plen), "Version: parse CC");
    CHECK(opcode == 0x1001, "Version: opcode");
    CHECK(status == 0, "Version: status OK");
    CHECK(plen >= 9, "Version: params length");
    CHECK(params[1] == 0x09, "Version: HCI 5.0");
}

static void test_read_bd_addr() {
    pt::BleHci hci;
    auto cmd = hci_cmd(0x1009);
    hci.feed_hci(cmd.data(), cmd.size());

    auto& tx = hci.tx_buffer();
    uint16_t opcode; uint8_t status, plen; const uint8_t* params;
    CHECK(parse_cc(tx, 0, &opcode, &status, &params, &plen), "BD_ADDR: parse CC");
    CHECK(opcode == 0x1009, "BD_ADDR: opcode");
    CHECK(status == 0, "BD_ADDR: status OK");
    CHECK(plen >= 7, "BD_ADDR: params length");
}

static void test_le_read_buffer_size() {
    pt::BleHci hci;
    auto cmd = hci_cmd(0x2002);
    hci.feed_hci(cmd.data(), cmd.size());

    auto& tx = hci.tx_buffer();
    uint16_t opcode; uint8_t status, plen; const uint8_t* params;
    CHECK(parse_cc(tx, 0, &opcode, &status, &params, &plen), "Buffer: parse CC");
    CHECK(opcode == 0x2002, "Buffer: opcode");
    CHECK(status == 0, "Buffer: status OK");
    // LE_ACL_Data_Packet_Length = 251
    CHECK(plen >= 4, "Buffer: params length");
    CHECK(params[1] == 251, "Buffer: pkt length 251");
    CHECK(params[3] == 8, "Buffer: 8 packets");
}

static void test_scanning() {
    pt::BleHci hci;

    // Set scan params
    uint8_t scan_params[7] = {0x01, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00};
    auto cmd1 = hci_cmd(0x200B, scan_params, 7);
    hci.feed_hci(cmd1.data(), cmd1.size());

    // Enable scanning
    uint8_t enable[2] = {0x01, 0x00};
    auto cmd2 = hci_cmd(0x200C, enable, 2);
    hci.feed_hci(cmd2.data(), cmd2.size());

    CHECK(hci.is_scanning(), "Scanning enabled");

    // Disable scanning
    uint8_t disable[2] = {0x00, 0x00};
    auto cmd3 = hci_cmd(0x200C, disable, 2);
    hci.feed_hci(cmd3.data(), cmd3.size());

    CHECK(!hci.is_scanning(), "Scanning disabled");
}

static void test_adv_capture() {
    pt::Bus bus;
    pt::RAM sram{1024};
    pt::NVIC nvic;
    pt::Radio radio;
    pt::BleHci hci;

    bus.map(0x20000000, 1024, &sram);
    radio.set_nvic(&nvic, 1);
    radio.set_bus(&bus);
    hci.set_bus(&bus);
    hci.set_radio(&radio);

    // Simulate: write an ADV_IND PDU to SRAM, configure RADIO, do TX
    uint32_t pkt_addr = 0x20000100;
    // S0: ADV_IND (type=0), TxAdd=1 (random)
    bus.write8(pkt_addr, 0x40); // PDU type=0, TxAdd=1
    bus.write8(pkt_addr + 1, 12); // Length: 6 (addr) + 6 (data)
    // AdvA: 11:22:33:44:55:66
    bus.write8(pkt_addr + 2, 0x11);
    bus.write8(pkt_addr + 3, 0x22);
    bus.write8(pkt_addr + 4, 0x33);
    bus.write8(pkt_addr + 5, 0x44);
    bus.write8(pkt_addr + 6, 0x55);
    bus.write8(pkt_addr + 7, 0x66);
    // AdvData: 5 bytes AD: len=4, type=0x09 (Complete Local Name), "Foo"
    bus.write8(pkt_addr + 8, 0x04);
    bus.write8(pkt_addr + 9, 0x09);
    bus.write8(pkt_addr + 10, 'F');
    bus.write8(pkt_addr + 11, 'o');
    bus.write8(pkt_addr + 12, 'o');
    bus.write8(pkt_addr + 13, 0x00);

    // Configure RADIO
    radio.write(0x504, pt::Width::Word, pkt_addr);  // PACKETPTR
    radio.write(0x554, pt::Width::Word, 37);         // DATAWHITEIV = channel 37
    radio.write(0x200, pt::Width::Word, (1<<0)|(1<<1)); // SHORTS: READY_START + END_DISABLE

    // Trigger TX
    radio.write(0x000, pt::Width::Word, 1); // TXEN

    // Check capture
    auto& adv = hci.last_adv();
    CHECK(adv.valid, "ADV captured");
    CHECK(adv.pdu_type == 0, "ADV type = ADV_IND");
    CHECK(adv.tx_add == 1, "ADV TxAdd = random");
    CHECK(adv.addr[0] == 0x11 && adv.addr[5] == 0x66, "ADV address");
    CHECK(adv.data_len == 6, "ADV data length");
    CHECK(adv.channel == 37, "ADV channel 37");
}

static void test_adv_report_generation() {
    pt::Bus bus;
    pt::RAM sram{1024};
    pt::NVIC nvic;
    pt::Radio radio;
    pt::BleHci hci;

    bus.map(0x20000000, 1024, &sram);
    radio.set_nvic(&nvic, 1);
    radio.set_bus(&bus);
    hci.set_bus(&bus);
    hci.set_radio(&radio);

    // Set up advertising (same as above)
    uint32_t pkt_addr = 0x20000100;
    bus.write8(pkt_addr, 0x40);
    bus.write8(pkt_addr + 1, 8);
    for (int i = 0; i < 6; i++) bus.write8(pkt_addr + 2 + i, 0xAA + i);
    bus.write8(pkt_addr + 8, 0x01);
    bus.write8(pkt_addr + 9, 0x02);

    radio.write(0x504, pt::Width::Word, pkt_addr);
    radio.write(0x554, pt::Width::Word, 38);
    radio.write(0x200, pt::Width::Word, 3);
    radio.write(0x000, pt::Width::Word, 1); // TX

    CHECK(hci.last_adv().valid, "ADV captured for report");

    // Enable scanning and trigger report
    uint8_t scan_en[2] = {1, 0};
    auto cmd = hci_cmd(0x200C, scan_en, 2);
    hci.feed_hci(cmd.data(), cmd.size());
    hci.tx_buffer().clear(); // Clear CC response

    // Poll with enough time delta to trigger report
    hci.poll(10'000'000);

    auto& tx = hci.tx_buffer();
    CHECK(tx.size() > 0, "Report generated");
    if (tx.size() > 3) {
        CHECK(tx[0] == 0x04, "Report: H4 EVT");
        CHECK(tx[1] == 0x3E, "Report: LE Meta Event");
        CHECK(tx[3] == 0x02, "Report: Subevent = Advertising Report");
    }
}

static void test_multiple_commands() {
    pt::BleHci hci;

    // Send Reset + Read_Local_Version in one batch
    auto cmd1 = hci_cmd(0x0C03);
    auto cmd2 = hci_cmd(0x1001);
    std::vector<uint8_t> batch;
    batch.insert(batch.end(), cmd1.begin(), cmd1.end());
    batch.insert(batch.end(), cmd2.begin(), cmd2.end());
    hci.feed_hci(batch.data(), batch.size());

    auto& tx = hci.tx_buffer();
    // Should have two Command Complete events
    CHECK(tx.size() > 10, "Multiple CCs generated");

    uint16_t op1, op2; uint8_t s1, s2, pl1, pl2; const uint8_t *p1, *p2;
    bool ok1 = parse_cc(tx, 0, &op1, &s1, &p1, &pl1);
    CHECK(ok1 && op1 == 0x0C03, "Batch: first CC is Reset");

    // Find second CC (after first event)
    size_t off2 = 3 + tx[2] + 1; // H4(1) + event_code(1) + len(1) + data(len) ... next H4
    // Actually: first event starts at 0, length is 1(H4) + 1(code) + 1(len) + tx[2](data)
    off2 = 3 + tx[2];
    bool ok2 = parse_cc(tx, off2, &op2, &s2, &p2, &pl2);
    CHECK(ok2 && op2 == 0x1001, "Batch: second CC is Version");
}

static void test_unknown_command() {
    pt::BleHci hci;
    auto cmd = hci_cmd(0xFFFF); // Nonsense opcode
    hci.feed_hci(cmd.data(), cmd.size());

    auto& tx = hci.tx_buffer();
    uint16_t opcode; uint8_t status, plen; const uint8_t* params;
    CHECK(parse_cc(tx, 0, &opcode, &status, &params, &plen), "Unknown: parse CC");
    CHECK(opcode == 0xFFFF, "Unknown: opcode echo");
    CHECK(status == 0x01, "Unknown: error status");
}

static void test_rx_injection() {
    pt::Bus bus;
    pt::RAM sram{1024};
    pt::NVIC nvic;
    pt::Radio radio;

    bus.map(0x20000000, 1024, &sram);
    radio.set_nvic(&nvic, 1);
    radio.set_bus(&bus);
    nvic.set_irq_pending_flag(nullptr); // No CPU

    // Configure RADIO for RX
    uint32_t pkt_addr = 0x20000200;
    radio.write(0x504, pt::Width::Word, pkt_addr);
    radio.write(0x554, pt::Width::Word, 37);

    // Enable RX
    radio.write(0x004, pt::Width::Word, 1); // RXEN
    radio.write(0x008, pt::Width::Word, 1); // START → state = RX

    CHECK(radio.get_state() == 3, "RX: state = RX"); // RX=3

    // Inject a packet
    uint8_t pdu[8] = {0x05, 0x06, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    radio.inject_rx(pdu, 8);

    // Check packet was written to memory
    CHECK(bus.read8(pkt_addr) == 0x05, "RX inject: S0 written");
    CHECK(bus.read8(pkt_addr + 1) == 0x06, "RX inject: LENGTH written");
    CHECK(bus.read8(pkt_addr + 2) == 0x11, "RX inject: payload[0]");

    // Check events were fired
    CHECK(radio.read(0x10C, pt::Width::Word) == 1, "RX inject: END event");
    CHECK(radio.read(0x130, pt::Width::Word) == 1, "RX inject: CRCOK event");
}

static void test_queue_rx() {
    pt::Bus bus;
    pt::RAM sram{1024};
    pt::NVIC nvic;
    pt::Radio radio;

    bus.map(0x20000000, 1024, &sram);
    radio.set_nvic(&nvic, 1);
    radio.set_bus(&bus);

    uint32_t pkt_addr = 0x20000200;
    radio.write(0x504, pt::Width::Word, pkt_addr);

    // Queue before RX is enabled
    uint8_t pdu[4] = {0xAA, 0x02, 0xBB, 0xCC};
    radio.queue_rx(pdu, 4);

    // Not in RX yet — check_pending_rx should not inject
    CHECK(!radio.check_pending_rx(), "Queue: not delivered before RX");

    // Enable RX
    radio.write(0x004, pt::Width::Word, 1);
    radio.write(0x008, pt::Width::Word, 1);
    CHECK(radio.get_state() == 3, "Queue: in RX state");

    // Now check_pending_rx should inject
    CHECK(radio.check_pending_rx(), "Queue: delivered in RX");
    CHECK(bus.read8(pkt_addr) == 0xAA, "Queue: data written");
}

int main() {
    test_hci_reset();
    test_read_local_version();
    test_read_bd_addr();
    test_le_read_buffer_size();
    test_scanning();
    test_adv_capture();
    test_adv_report_generation();
    test_multiple_commands();
    test_unknown_command();
    test_rx_injection();
    test_queue_rx();

    fprintf(stderr, "[test_ble_hci] %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
