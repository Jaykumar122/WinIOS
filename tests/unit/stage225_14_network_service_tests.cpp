#include <iostream>
#include <cstdint>
#include <vector>
#include <cstring>

#include "devices/network_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 225.14] Virtual Network Device & Loopback Architecture...\n" << std::flush;

    VirtualNetworkService net;
    TEST_ASSERT(net.initialize());
    TEST_ASSERT(net.start());

    // 1. Initial state
    auto st = net.get_stats();
    TEST_ASSERT(st.packets_sent == 0);
    TEST_ASSERT(st.packets_received == 0);
    TEST_ASSERT(st.packets_dropped == 0);
    TEST_ASSERT(st.queue_depth == 0);
    TEST_ASSERT(st.queue_capacity == 64);
    TEST_ASSERT(st.mtu == 1514);

    // 2. Loopback packet transmission & reception
    const char msg[] = "WINIOS_VIRTUAL_LOOPBACK_PACKET_TEST";
    TEST_ASSERT(net.send_packet(reinterpret_cast<const uint8_t*>(msg), sizeof(msg)));
    TEST_ASSERT(net.queue_depth() == 1);
    TEST_ASSERT(net.has_pending_packets());

    uint8_t rx_buf[128]{0};
    size_t rx_len = 0;
    TEST_ASSERT(net.receive_packet(rx_buf, sizeof(rx_buf), &rx_len));
    TEST_ASSERT(rx_len == sizeof(msg));
    TEST_ASSERT(std::memcmp(rx_buf, msg, sizeof(msg)) == 0);
    TEST_ASSERT(net.queue_depth() == 0);

    // 3. Reject invalid packet lengths (0 or > MTU)
    TEST_ASSERT(!net.send_packet(reinterpret_cast<const uint8_t*>(msg), 0));
    std::vector<uint8_t> big_packet(1515, 0xFF);
    TEST_ASSERT(!net.send_packet(big_packet.data(), big_packet.size()));

    // 4. Queue capacity and packet dropping on saturation (64 packets max)
    const uint8_t dummy = 0xAA;
    for (int i = 0; i < 64; ++i) {
        TEST_ASSERT(net.send_packet(&dummy, 1));
    }
    TEST_ASSERT(net.queue_depth() == 64);

    // 65th packet must be dropped
    TEST_ASSERT(!net.send_packet(&dummy, 1));
    st = net.get_stats();
    TEST_ASSERT(st.packets_dropped == 1);
    TEST_ASSERT(st.packets_sent == 65); // 1 from earlier + 64

    // Flush queue
    net.flush_queue();
    TEST_ASSERT(net.queue_depth() == 0);

    std::cout << "[Test Stage 225.14] PASS: Network loopback service verified.\n";
    return 0;
}
