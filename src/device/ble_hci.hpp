#pragma once
#include "../bus.hpp"
#include "radio.hpp"
#include "ble_companion.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

// POSIX sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace pt {

// HCI H4 packet indicators
static constexpr uint8_t H4_CMD = 0x01;
static constexpr uint8_t H4_ACL = 0x02;
static constexpr uint8_t H4_EVT = 0x04;

// BLE advertising PDU types
static constexpr uint8_t ADV_IND         = 0x00;
static constexpr uint8_t ADV_DIRECT_IND  = 0x01;
static constexpr uint8_t ADV_NONCONN_IND = 0x02;
static constexpr uint8_t SCAN_REQ        = 0x03;
static constexpr uint8_t SCAN_RSP        = 0x04;
static constexpr uint8_t CONNECT_IND     = 0x05;
static constexpr uint8_t ADV_SCAN_IND    = 0x06;

// LL Control opcodes
static constexpr uint8_t LL_CONNECTION_UPDATE_IND = 0x00;
static constexpr uint8_t LL_CHANNEL_MAP_IND       = 0x01;
static constexpr uint8_t LL_TERMINATE_IND          = 0x02;
static constexpr uint8_t LL_ENC_REQ               = 0x03;
static constexpr uint8_t LL_ENC_RSP               = 0x04;
static constexpr uint8_t LL_START_ENC_REQ         = 0x05;
static constexpr uint8_t LL_START_ENC_RSP         = 0x06;
static constexpr uint8_t LL_UNKNOWN_RSP           = 0x07;
static constexpr uint8_t LL_FEATURE_REQ           = 0x08;
static constexpr uint8_t LL_FEATURE_RSP           = 0x09;
static constexpr uint8_t LL_PAUSE_ENC_REQ         = 0x0A;
static constexpr uint8_t LL_PAUSE_ENC_RSP         = 0x0B;
static constexpr uint8_t LL_VERSION_IND           = 0x0C;
static constexpr uint8_t LL_REJECT_IND            = 0x0D;
static constexpr uint8_t LL_SLAVE_FEATURE_REQ     = 0x0E;
static constexpr uint8_t LL_LENGTH_REQ            = 0x14;
static constexpr uint8_t LL_LENGTH_RSP            = 0x15;
static constexpr uint8_t LL_PHY_REQ               = 0x16;
static constexpr uint8_t LL_PHY_RSP               = 0x17;
static constexpr uint8_t LL_PHY_UPDATE_IND        = 0x18;
static constexpr uint8_t LL_PING_REQ              = 0x12;
static constexpr uint8_t LL_PING_RSP              = 0x13;

class BleHci {
    Bus* bus_ = nullptr;
    Radio* radio_ = nullptr;

    // TCP server
    int server_fd_ = -1;
    int client_fd_ = -1;
    uint16_t port_ = 9001;

    // HCI state
    bool scanning_ = false;
    bool connecting_ = false;
    bool connected_ = false;
    uint16_t conn_handle_ = 0x0001;

    // Connection parameters (in BLE units)
    uint16_t conn_interval_ = 24;   // 30ms (24 * 1.25ms)
    uint16_t conn_latency_ = 0;
    uint16_t conn_timeout_ = 200;   // 2s (200 * 10ms)
    uint32_t conn_access_addr_ = 0x50655272; // Random but valid
    uint32_t conn_crcinit_ = 0x012345;

    // Captured advertising
    struct AdvCapture {
        uint8_t pdu_type = 0;
        uint8_t tx_add = 0;
        uint8_t addr[6] = {};
        uint8_t data[31] = {};
        uint8_t data_len = 0;
        uint8_t channel = 0;
        bool valid = false;
    } last_adv_;
    uint64_t last_adv_report_cy_ = 0;

    // Data channel sequence numbers
    uint8_t our_sn_ = 0;    // Our SN for outgoing PDUs
    uint8_t our_nesn_ = 0;  // Our NESN (next expected from peer)

    // Companion mode
    BleCompanion companion_;
    bool companion_mode_ = false;
    bool companion_started_ = false;

    // I/O buffers
    std::vector<uint8_t> tx_buf_;
    std::vector<uint8_t> rx_buf_;

    // PRNG for access address / CRC init
    uint32_t prng_state_ = 0xDEADBEEF;

    // Initiator address (presented to host as BD_ADDR)
    uint8_t local_addr_[6] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};

public:
    void set_bus(Bus* b) { bus_ = b; }
    void set_radio(Radio* r) {
        radio_ = r;
        if (r) r->set_tx_handler(radio_tx_cb, this);
    }
    void set_companion_mode(bool on) { companion_mode_ = on; }

    bool start(uint16_t port = 9001) {
        port_ = port;
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) return false;

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        fcntl(server_fd_, F_SETFL, O_NONBLOCK);

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(server_fd_); server_fd_ = -1; return false;
        }
        if (listen(server_fd_, 1) < 0) {
            close(server_fd_); server_fd_ = -1; return false;
        }
        fprintf(stderr, "[BLE-HCI] Listening on port %d\n", port);
        return true;
    }

    void poll(uint64_t cycles) {
        poll_tcp();

        // Check for pending RX injection into Radio
        if (radio_) radio_->check_pending_rx();

        // Send advertising reports while scanning
        if (scanning_ && last_adv_.valid) {
            if (cycles - last_adv_report_cy_ >= 6'400'000) { // ~100ms
                send_adv_report();
                last_adv_report_cy_ = cycles;
            }
        }
    }

    ~BleHci() {
        if (client_fd_ >= 0) close(client_fd_);
        if (server_fd_ >= 0) close(server_fd_);
    }

    // For testing: bypass TCP
    void feed_hci(const uint8_t* data, size_t len) {
        rx_buf_.insert(rx_buf_.end(), data, data + len);
        process_rx();
    }
    std::vector<uint8_t>& tx_buffer() { return tx_buf_; }
    const AdvCapture& last_adv() const { return last_adv_; }
    bool is_scanning() const { return scanning_; }
    bool is_connected() const { return connected_; }
    bool is_companion_done() const { return companion_.is_done(); }

private:
    // ================================================================
    // Radio TX capture callback
    // ================================================================
    static void radio_tx_cb(void* ctx, const uint8_t* pdu, size_t len, uint8_t channel) {
        static_cast<BleHci*>(ctx)->on_radio_tx(pdu, len, channel);
    }

    void on_radio_tx(const uint8_t* pdu, size_t len, uint8_t channel) {
        if (len < 2) return;

        bool is_adv_channel = (channel == 37 || channel == 38 || channel == 39);

        if (is_adv_channel) {
            on_adv_tx(pdu, len, channel);
        } else if (connected_) {
            on_data_tx(pdu, len);
        }
    }

    void on_adv_tx(const uint8_t* pdu, size_t len, uint8_t channel) {
        uint8_t pdu_type = pdu[0] & 0x0F;
        uint8_t tx_add = (pdu[0] >> 6) & 1;
        uint8_t pdu_len = pdu[1];

        if (pdu_len < 6 || 2 + pdu_len > (int)len) return;

        // Store captured advertising
        last_adv_.pdu_type = pdu_type;
        last_adv_.tx_add = tx_add;
        memcpy(last_adv_.addr, pdu + 2, 6);
        last_adv_.data_len = (pdu_len > 6) ? pdu_len - 6 : 0;
        if (last_adv_.data_len > 31) last_adv_.data_len = 31;
        if (last_adv_.data_len) memcpy(last_adv_.data, pdu + 8, last_adv_.data_len);
        last_adv_.channel = channel;
        last_adv_.valid = true;

        // Update local_addr_ from captured advertising (this is the device's address)
        memcpy(local_addr_, last_adv_.addr, 6);

        // Auto-connect in companion mode on first ADV_IND
        if (companion_mode_ && !connecting_ && !connected_ && pdu_type == ADV_IND) {
            connecting_ = true;
            fprintf(stderr, "[Companion] Auto-connecting to %02X:%02X:%02X:%02X:%02X:%02X\n",
                    last_adv_.addr[5], last_adv_.addr[4], last_adv_.addr[3],
                    last_adv_.addr[2], last_adv_.addr[1], last_adv_.addr[0]);
        }

        // If we're connecting, inject CONNECT_IND on the next RX window
        if (connecting_ && pdu_type == ADV_IND) {
            uint8_t conn_pdu[40];
            size_t conn_len;
            build_connect_ind(conn_pdu, &conn_len);
            radio_->queue_rx(conn_pdu, conn_len);
            connecting_ = false;
            connected_ = true;
            our_sn_ = our_nesn_ = 0;
            if (!companion_mode_)
                send_connection_complete();
            fprintf(stderr, "[BLE-HCI] Connected (handle=0x%04X)%s\n",
                    conn_handle_, companion_mode_ ? " [companion]" : "");
        }
    }

    void on_data_tx(const uint8_t* pdu, size_t len) {
        if (len < 2) return;
        uint8_t header = pdu[0];
        uint8_t pdu_len = pdu[1];
        uint8_t llid = header & 0x03;
        uint8_t sn = (header >> 3) & 1;

        // Acknowledge: update our NESN
        if (sn == our_nesn_) {
            our_nesn_ ^= 1;

            if (llid == 0x03 && pdu_len >= 1) {
                handle_ll_control(pdu + 2, pdu_len);
            } else if ((llid == 0x01 || llid == 0x02) && pdu_len > 0) {
                if (companion_mode_) {
                    // Route L2CAP to companion: strip L2CAP header (len(2)+CID(2)) → ATT
                    const uint8_t* l2cap = pdu + 2;
                    if (pdu_len >= 4) {
                        uint16_t l2cap_len = l2cap[0] | (l2cap[1] << 8);
                        uint16_t cid = l2cap[2] | (l2cap[3] << 8);
                        if (cid == 0x0004 && l2cap_len > 0 && 4 + l2cap_len <= pdu_len) {
                            if (!companion_started_) {
                                companion_started_ = true;
                                companion_.on_connected();
                            }
                            companion_.on_att_received(l2cap + 4, l2cap_len);
                        }
                    }
                } else {
                    send_acl_data(conn_handle_, pdu + 2, pdu_len);
                }
            }
        }

        // Send response: companion data or empty ACK
        if (radio_) {
            if (companion_mode_ && companion_started_ && companion_.has_pending()) {
                send_att_to_device();
            } else {
                uint8_t ack[2];
                ack[0] = 0x01 | (our_nesn_ << 2) | (our_sn_ << 3);
                ack[1] = 0;
                radio_->queue_rx(ack, 2);
            }
        }
    }

    void send_att_to_device() {
        uint8_t att[256];
        size_t att_len = companion_.get_pending(att);
        if (att_len == 0) return;

        // Wrap: data channel PDU [ header(1) | len(1) | L2CAP_len(2) | CID(2) | ATT... ]
        uint8_t pdu[265];
        pdu[0] = 0x02 | (our_nesn_ << 2) | (our_sn_ << 3); // LLID=10 (L2CAP start)
        pdu[1] = 4 + att_len; // L2CAP header + ATT
        pdu[2] = att_len; pdu[3] = att_len >> 8; // L2CAP length
        pdu[4] = 0x04; pdu[5] = 0x00; // CID = 0x0004 (ATT)
        memcpy(pdu + 6, att, att_len);
        our_sn_ ^= 1;
        radio_->queue_rx(pdu, 6 + att_len);
    }

    // ================================================================
    // LL Control PDU handling
    // ================================================================
    void handle_ll_control(const uint8_t* payload, size_t len) {
        if (len < 1) return;
        uint8_t opcode = payload[0];

        switch (opcode) {
        case LL_VERSION_IND:
            send_ll_response(LL_VERSION_IND, (const uint8_t*)"\x09\x59\x00\x00\x00", 5);
            break;
        case LL_FEATURE_REQ:
        case LL_SLAVE_FEATURE_REQ: {
            // Respond with our (minimal) features: LE Encryption, Conn Param Req, Data Length Ext
            uint8_t features[8] = {0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            send_ll_response(LL_FEATURE_RSP, features, 8);
            break;
        }
        case LL_LENGTH_REQ: {
            // Respond with our max RX/TX octets and times
            uint8_t rsp[8];
            rsp[0] = 251; rsp[1] = 0;   // MaxRxOctets
            rsp[2] = 0x48; rsp[3] = 0x08; // MaxRxTime (2120µs)
            rsp[4] = 251; rsp[5] = 0;   // MaxTxOctets
            rsp[6] = 0x48; rsp[7] = 0x08; // MaxTxTime
            send_ll_response(LL_LENGTH_RSP, rsp, 8);
            break;
        }
        case LL_PING_REQ:
            send_ll_response(LL_PING_RSP, nullptr, 0);
            break;
        case LL_PHY_REQ: {
            // Respond: we only support 1M PHY
            uint8_t rsp[2] = {0x01, 0x01}; // TX_PHYS=1M, RX_PHYS=1M
            send_ll_response(LL_PHY_RSP, rsp, 2);
            break;
        }
        case LL_TERMINATE_IND:
            connected_ = false;
            if (client_fd_ >= 0)
                send_disconnection(conn_handle_, payload[1]);
            break;
        default:
            // Unknown: send LL_UNKNOWN_RSP
            send_ll_response(LL_UNKNOWN_RSP, &opcode, 1);
            break;
        }
    }

    void send_ll_response(uint8_t ll_opcode, const uint8_t* data, size_t len) {
        if (!radio_) return;
        uint8_t pdu[259];
        pdu[0] = 0x03 | (our_nesn_ << 2) | (our_sn_ << 3); // LLID=11 (LL Control)
        pdu[1] = 1 + len; // opcode + data
        pdu[2] = ll_opcode;
        if (len) memcpy(pdu + 3, data, len);
        our_sn_ ^= 1;
        radio_->queue_rx(pdu, 3 + len);
    }

    // ================================================================
    // CONNECT_IND builder
    // ================================================================
    void build_connect_ind(uint8_t* out, size_t* out_len) {
        // PDU header
        out[0] = CONNECT_IND; // PDU type, TxAdd=0 (public), RxAdd based on adv
        if (last_adv_.tx_add) out[0] |= 0x80; // RxAdd = advertiser's TxAdd
        out[1] = 34; // Length: InitA(6) + AdvA(6) + LLData(22)

        // InitA: our initiator address (random)
        uint8_t init_addr[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0xC0}; // Random static
        memcpy(out + 2, init_addr, 6);
        out[0] |= 0x40; // TxAdd=1 (random)

        // AdvA: advertiser address
        memcpy(out + 8, last_adv_.addr, 6);

        // LLData (22 bytes)
        uint8_t* ll = out + 14;

        // Access Address (4 bytes)
        conn_access_addr_ = prng();
        conn_access_addr_ |= 0x40000000; // Ensure valid (not all 0s/1s)
        ll[0] = conn_access_addr_;
        ll[1] = conn_access_addr_ >> 8;
        ll[2] = conn_access_addr_ >> 16;
        ll[3] = conn_access_addr_ >> 24;

        // CRCInit (3 bytes)
        conn_crcinit_ = prng() & 0xFFFFFF;
        ll[4] = conn_crcinit_;
        ll[5] = conn_crcinit_ >> 8;
        ll[6] = conn_crcinit_ >> 16;

        // WinSize (1 byte) — transmit window = WinSize * 1.25ms
        ll[7] = 2; // 2.5ms window

        // WinOffset (2 bytes LE) — 0
        ll[8] = 0; ll[9] = 0;

        // Interval (2 bytes LE) — in 1.25ms units
        ll[10] = conn_interval_; ll[11] = conn_interval_ >> 8;

        // Latency (2 bytes LE)
        ll[12] = conn_latency_; ll[13] = conn_latency_ >> 8;

        // Timeout (2 bytes LE) — in 10ms units
        ll[14] = conn_timeout_; ll[15] = conn_timeout_ >> 8;

        // Channel Map (5 bytes) — all 37 data channels enabled
        ll[16] = 0xFF; ll[17] = 0xFF; ll[18] = 0xFF; ll[19] = 0xFF; ll[20] = 0x1F;

        // Hop (5 bits) + SCA (3 bits)
        ll[21] = 7 | (0 << 5); // Hop=7, SCA=0 (251-500ppm)

        *out_len = 36; // 2 header + 34 payload
    }

    uint32_t prng() {
        prng_state_ = prng_state_ * 1103515245 + 12345;
        return prng_state_ >> 1; // Drop low bit
    }

    // ================================================================
    // TCP I/O
    // ================================================================
    void poll_tcp() {
        // Accept
        if (server_fd_ >= 0 && client_fd_ < 0) {
            int fd = accept(server_fd_, nullptr, nullptr);
            if (fd >= 0) {
                fcntl(fd, F_SETFL, O_NONBLOCK);
                int flag = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                client_fd_ = fd;
                scanning_ = false;
                connecting_ = false;
                connected_ = false;
                fprintf(stderr, "[BLE-HCI] Client connected\n");
            }
        }

        // Read
        if (client_fd_ >= 0) {
            uint8_t buf[1024];
            ssize_t n = recv(client_fd_, buf, sizeof(buf), 0);
            if (n > 0) {
                rx_buf_.insert(rx_buf_.end(), buf, buf + n);
                process_rx();
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                fprintf(stderr, "[BLE-HCI] Client disconnected\n");
                close(client_fd_);
                client_fd_ = -1;
                scanning_ = false;
                connecting_ = false;
                connected_ = false;
            }
        }

        // Flush TX
        if (client_fd_ >= 0 && !tx_buf_.empty()) {
            ssize_t sent = send(client_fd_, tx_buf_.data(), tx_buf_.size(), MSG_NOSIGNAL);
            if (sent > 0)
                tx_buf_.erase(tx_buf_.begin(), tx_buf_.begin() + sent);
        }
    }

    // ================================================================
    // HCI RX processing
    // ================================================================
    void process_rx() {
        while (rx_buf_.size() >= 1) {
            uint8_t type = rx_buf_[0];

            if (type == H4_CMD) {
                if (rx_buf_.size() < 4) return;
                uint16_t opcode = rx_buf_[1] | (rx_buf_[2] << 8);
                uint8_t plen = rx_buf_[3];
                if (rx_buf_.size() < 4u + plen) return;
                handle_command(opcode, rx_buf_.data() + 4, plen);
                rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 4 + plen);
            } else if (type == H4_ACL) {
                if (rx_buf_.size() < 5) return;
                uint16_t acl_len = rx_buf_[3] | (rx_buf_[4] << 8);
                if (rx_buf_.size() < 5u + acl_len) return;
                handle_acl_from_host(rx_buf_.data() + 1, 4 + acl_len);
                rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 5 + acl_len);
            } else {
                rx_buf_.erase(rx_buf_.begin()); // Skip unknown
            }
        }
    }

    // ================================================================
    // HCI Command handling
    // ================================================================
    void handle_command(uint16_t opcode, const uint8_t* params, uint8_t plen) {
        (void)plen;

        switch (opcode) {
        case 0x0C03: { // HCI_Reset
            scanning_ = false;
            connecting_ = false;
            connected_ = false;
            our_sn_ = our_nesn_ = 0;
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x1001: { // Read_Local_Version_Information
            uint8_t rsp[9] = {};
            rsp[0] = 0;    // Status
            rsp[1] = 0x09; // HCI Version: BT 5.0
            rsp[2] = 0; rsp[3] = 0; // HCI Revision
            rsp[4] = 0x09; // LMP Version: BT 5.0
            rsp[5] = 0x59; rsp[6] = 0x00; // Nordic Semiconductor
            rsp[7] = 0; rsp[8] = 0; // LMP Subversion
            send_cc(opcode, rsp, 9);
            break;
        }
        case 0x1009: { // Read_BD_ADDR
            uint8_t rsp[7] = {};
            rsp[0] = 0;
            memcpy(rsp + 1, local_addr_, 6);
            send_cc(opcode, rsp, 7);
            break;
        }
        case 0x1002: { // Read_Local_Supported_Commands
            uint8_t rsp[65] = {};
            rsp[0] = 0; // Status
            // Set bits for supported commands
            rsp[1 + 0] = 0x20;  // Disconnect
            rsp[1 + 5] = 0x40;  // Set Event Mask
            rsp[1 + 5] |= 0x80; // Reset
            rsp[1 + 14] = 0x08; // Read Local Version
            rsp[1 + 14] |= 0x10; // Read Local Supported Commands
            rsp[1 + 14] |= 0x20; // Read Local Supported Features
            rsp[1 + 15] = 0x02; // Read BD_ADDR
            rsp[1 + 25] = 0x01; // LE Set Event Mask
            rsp[1 + 25] |= 0x02; // LE Read Buffer Size
            rsp[1 + 25] |= 0x04; // LE Read Local P-256
            rsp[1 + 25] |= 0x10; // LE Set Random Address
            rsp[1 + 26] = 0x04; // LE Set Scan Parameters
            rsp[1 + 26] |= 0x08; // LE Set Scan Enable
            rsp[1 + 26] |= 0x10; // LE Create Connection
            rsp[1 + 26] |= 0x20; // LE Create Connection Cancel
            send_cc(opcode, rsp, 65);
            break;
        }
        case 0x1003: { // Read_Local_Supported_Features
            uint8_t rsp[9] = {};
            rsp[0] = 0;
            rsp[1 + 4] = 0x60; // LE Supported + BR/EDR Not Supported
            send_cc(opcode, rsp, 9);
            break;
        }
        case 0x0C01: { // Set_Event_Mask
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x0C63: { // Set_Event_Mask_Page_2
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x2001: { // LE_Set_Event_Mask
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x2002: { // LE_Read_Buffer_Size [v1]
            uint8_t rsp[4] = {};
            rsp[0] = 0; // Status
            rsp[1] = 251; rsp[2] = 0; // LE_ACL_Data_Packet_Length = 251
            rsp[3] = 8; // Total_Num_LE_ACL_Data_Packets = 8
            send_cc(opcode, rsp, 4);
            break;
        }
        case 0x2003: { // LE_Read_Local_P-256_Public_Key... no, LE_Read_Local_Supported_Features
            uint8_t rsp[9] = {};
            rsp[0] = 0;
            rsp[1] = 0x21; // LE Encryption + Connection Parameters Request
            send_cc(opcode, rsp, 9);
            break;
        }
        case 0x2005: { // LE_Set_Random_Address
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x200B: { // LE_Set_Scan_Parameters
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x200C: { // LE_Set_Scan_Enable
            scanning_ = params[0] != 0;
            if (scanning_)
                fprintf(stderr, "[BLE-HCI] Scanning %s\n", scanning_ ? "ON" : "OFF");
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x200D: { // LE_Create_Connection
            // params: scan_interval(2), scan_window(2), filter_policy(1),
            //         peer_addr_type(1), peer_addr(6), own_addr_type(1),
            //         conn_interval_min(2), conn_interval_max(2), conn_latency(2),
            //         supervision_timeout(2), min_ce(2), max_ce(2)
            if (plen >= 25) {
                conn_interval_ = params[12] | (params[13] << 8);
                conn_latency_ = params[16] | (params[17] << 8);
                conn_timeout_ = params[18] | (params[19] << 8);
            }
            connecting_ = true;
            scanning_ = false;
            fprintf(stderr, "[BLE-HCI] Connecting (interval=%d latency=%d timeout=%d)\n",
                    conn_interval_, conn_latency_, conn_timeout_);
            send_cs(0, opcode); // Command Status: success (pending)
            break;
        }
        case 0x200E: { // LE_Create_Connection_Cancel
            connecting_ = false;
            uint8_t status = connecting_ ? 0 : 0x0C; // Command Disallowed if not connecting
            send_cc(opcode, &status, 1);
            break;
        }
        case 0x200F: { // LE_Read_Filter_Accept_List_Size
            uint8_t rsp[2] = {0, 8}; // Status, Size
            send_cc(opcode, rsp, 2);
            break;
        }
        case 0x1005: { // Read_Buffer_Size (classic)
            uint8_t rsp[8] = {};
            rsp[0] = 0; // Status, all zeros = no classic buffers
            send_cc(opcode, rsp, 8);
            break;
        }
        case 0x2060: { // LE_Read_Buffer_Size [v2]
            uint8_t rsp[7] = {};
            rsp[0] = 0; // Status
            rsp[1] = 251; rsp[2] = 0; // LE_ACL_Data_Packet_Length = 251
            rsp[3] = 8; // Total_Num_LE_ACL_Data_Packets
            // ISO fields = 0
            send_cc(opcode, rsp, 7);
            break;
        }
        case 0x2074: { // LE_Set_Host_Feature
            uint8_t status = 0;
            send_cc(opcode, &status, 1);
            break;
        }
        default:
            fprintf(stderr, "[BLE-HCI] Unknown command 0x%04X (plen=%d)\n", opcode, plen);
            // Return unknown command error
            uint8_t status = 0x01; // Unknown HCI Command
            send_cc(opcode, &status, 1);
            break;
        }
    }

    // ================================================================
    // HCI ACL from host → Radio RX injection
    // ================================================================
    void handle_acl_from_host(const uint8_t* acl, size_t len) {
        if (len < 4) return;
        // uint16_t handle_flags = acl[0] | (acl[1] << 8);
        uint16_t acl_len = acl[2] | (acl[3] << 8);
        const uint8_t* l2cap = acl + 4;

        if (!connected_ || !radio_) return;

        // Wrap as data channel PDU
        uint8_t pdu[259];
        pdu[0] = 0x02 | (our_nesn_ << 2) | (our_sn_ << 3); // LLID=10 (L2CAP start), NESN, SN
        pdu[1] = acl_len;
        memcpy(pdu + 2, l2cap, acl_len);
        our_sn_ ^= 1;
        radio_->queue_rx(pdu, 2 + acl_len);

        // Send Num Completed Packets
        send_num_completed(conn_handle_, 1);
    }

    // ================================================================
    // HCI event builders
    // ================================================================
    void send_event(uint8_t code, const uint8_t* data, uint8_t len) {
        tx_buf_.push_back(H4_EVT);
        tx_buf_.push_back(code);
        tx_buf_.push_back(len);
        tx_buf_.insert(tx_buf_.end(), data, data + len);
    }

    void send_cc(uint16_t opcode, const uint8_t* params, uint8_t plen) {
        uint8_t buf[258];
        buf[0] = 1; // Num_HCI_Command_Packets
        buf[1] = opcode & 0xFF;
        buf[2] = opcode >> 8;
        memcpy(buf + 3, params, plen);
        send_event(0x0E, buf, 3 + plen); // Command Complete
    }

    void send_cs(uint8_t status, uint16_t opcode) {
        uint8_t buf[4];
        buf[0] = status;
        buf[1] = 1; // Num_HCI_Command_Packets
        buf[2] = opcode & 0xFF;
        buf[3] = opcode >> 8;
        send_event(0x0F, buf, 4); // Command Status
    }

    void send_adv_report() {
        // LE Meta Event: Advertising Report
        uint8_t buf[50];
        buf[0] = 0x02; // Subevent: LE_Advertising_Report
        buf[1] = 1;    // Num_Reports

        // Event_Type: map from PDU type
        uint8_t evt_type;
        switch (last_adv_.pdu_type) {
        case ADV_IND: evt_type = 0x00; break;
        case ADV_DIRECT_IND: evt_type = 0x01; break;
        case ADV_SCAN_IND: evt_type = 0x02; break;
        case ADV_NONCONN_IND: evt_type = 0x03; break;
        case SCAN_RSP: evt_type = 0x04; break;
        default: evt_type = 0x00;
        }
        buf[2] = evt_type;
        buf[3] = last_adv_.tx_add; // Address_Type (0=public, 1=random)
        memcpy(buf + 4, last_adv_.addr, 6);
        buf[10] = last_adv_.data_len;
        memcpy(buf + 11, last_adv_.data, last_adv_.data_len);
        buf[11 + last_adv_.data_len] = (uint8_t)(int8_t)-60; // RSSI: -60 dBm

        send_event(0x3E, buf, 12 + last_adv_.data_len); // LE Meta Event
    }

    void send_connection_complete() {
        uint8_t buf[19];
        buf[0] = 0x01; // Subevent: LE_Connection_Complete
        buf[1] = 0;    // Status: success
        buf[2] = conn_handle_ & 0xFF;
        buf[3] = conn_handle_ >> 8;
        buf[4] = 0x01; // Role: Slave (we connected TO the device, device is peripheral)
                        // Wait — we're the initiator (central). But from HCI host perspective:
                        // The host asked us (controller) to create a connection.
                        // We're the central/master. Role = 0x00 (Central).
        buf[4] = 0x00; // Role: Central
        buf[5] = last_adv_.tx_add; // Peer_Address_Type
        memcpy(buf + 6, last_adv_.addr, 6);
        buf[12] = conn_interval_ & 0xFF;
        buf[13] = conn_interval_ >> 8;
        buf[14] = conn_latency_ & 0xFF;
        buf[15] = conn_latency_ >> 8;
        buf[16] = conn_timeout_ & 0xFF;
        buf[17] = conn_timeout_ >> 8;
        buf[18] = 0; // Central_Clock_Accuracy

        send_event(0x3E, buf, 19);
    }

    void send_acl_data(uint16_t handle, const uint8_t* data, size_t len) {
        // H4 ACL packet
        tx_buf_.push_back(H4_ACL);
        uint16_t handle_flags = handle | (0x02 << 12); // PB=10 (start, auto-flush)
        tx_buf_.push_back(handle_flags & 0xFF);
        tx_buf_.push_back(handle_flags >> 8);
        tx_buf_.push_back(len & 0xFF);
        tx_buf_.push_back(len >> 8);
        tx_buf_.insert(tx_buf_.end(), data, data + len);
    }

    void send_num_completed(uint16_t handle, uint16_t count) {
        uint8_t buf[5];
        buf[0] = 1; // Number_of_Handles
        buf[1] = handle & 0xFF;
        buf[2] = handle >> 8;
        buf[3] = count & 0xFF;
        buf[4] = count >> 8;
        send_event(0x13, buf, 5); // Number_Of_Completed_Packets
    }

    void send_disconnection(uint16_t handle, uint8_t reason) {
        uint8_t buf[4];
        buf[0] = 0;    // Status
        buf[1] = handle & 0xFF;
        buf[2] = handle >> 8;
        buf[3] = reason;
        send_event(0x05, buf, 4); // Disconnection Complete
    }
};

} // namespace pt
