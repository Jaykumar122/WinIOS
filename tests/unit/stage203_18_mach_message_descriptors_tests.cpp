// ==============================================================================
// WinIOS Campaign 203 Stage 203.18: Mach Message Descriptors & Complex Messages Tests
// ==============================================================================
// Independent verification of:
// 1. MACH_MSGH_BITS_COMPLEX and MachMsgBodyRaw descriptor count
// 2. Port Descriptors with MACH_MSG_TYPE_MAKE_SEND (send right creation)
// 3. Port Descriptors with MACH_MSG_TYPE_MOVE_RECEIVE (receive right transfer)
// 4. Out-Of-Line (OOL) Memory Descriptors with MACH_MSG_OOL_DESCRIPTOR & deallocation
// 5. Mixed complex message with multiple descriptors and inline trailer payload
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "runtime/darwin_mach_ipc.h"
#include "memory/memory.h"

using namespace winios::runtime;
using namespace winios;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Port Descriptor with MACH_MSG_TYPE_MAKE_SEND
// -----------------------------------------------------------------------------
void test_port_descriptor_make_send() {
    std::cout << "[Stage 203.18 Test 1] Port Descriptor (MAKE_SEND)...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 701;
    const std::uint32_t receiver_task = 702;

    auto snd_space = ipc.create_task_port_space(sender_task);
    auto rcv_space = ipc.create_task_port_space(receiver_task);

    // Destination port for receiver
    mach_port_t rcv_dest = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    auto dest_obj = rcv_space->lookup_port(rcv_dest);
    mach_port_t snd_dest = snd_space->allocate_port(dest_obj, kMachPortRightSend);

    // Sender's own port to transfer
    mach_port_t my_port = ipc.mach_port_allocate(sender_task, kMachPortRightReceive);

    Memory mem(0x100000); // 1 MB
    const std::uint64_t snd_addr = 0x1000;

    // Build complex message
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = MACH_MSGH_BITS_COMPLEX;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw) + sizeof(MachMsgPortDescriptorRaw);
    hdr.msgh_remote_port = snd_dest;
    hdr.msgh_local_port = kMachPortNull;
    hdr.msgh_id = 2001;

    MachMsgBodyRaw body{};
    body.msgh_descriptor_count = 1;

    MachMsgPortDescriptorRaw pdesc{};
    pdesc.name = my_port;
    pdesc.disposition = static_cast<std::uint8_t>(MACH_MSG_TYPE_MAKE_SEND);
    pdesc.type = MACH_MSG_PORT_DESCRIPTOR;

    std::memcpy(mem.raw_data() + snd_addr, &hdr, sizeof(hdr));
    std::memcpy(mem.raw_data() + snd_addr + sizeof(hdr), &body, sizeof(body));
    std::memcpy(mem.raw_data() + snd_addr + sizeof(hdr) + sizeof(body), &pdesc, sizeof(pdesc));

    int sr = ipc.mach_msg(sender_task, snd_addr, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(sr == kMachMsgSuccess, "Send complex message with port descriptor succeeded");

    // Receive in receiver
    const std::uint64_t rcv_addr = 0x4000;
    int rr = ipc.mach_msg(receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, rcv_dest, 0, mem);
    TEST_CHECK(rr == kMachMsgSuccess, "Receive complex message succeeded");

    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + rcv_addr, sizeof(rcv_hdr));
    TEST_CHECK((rcv_hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX) != 0, "Received header has COMPLEX bit set");

    MachMsgBodyRaw rcv_body{};
    std::memcpy(&rcv_body, mem.raw_data() + rcv_addr + sizeof(rcv_hdr), sizeof(rcv_body));
    TEST_CHECK(rcv_body.msgh_descriptor_count == 1, "Received body indicates 1 descriptor");

    MachMsgPortDescriptorRaw rcv_pdesc{};
    std::memcpy(&rcv_pdesc, mem.raw_data() + rcv_addr + sizeof(rcv_hdr) + sizeof(rcv_body), sizeof(rcv_pdesc));
    TEST_CHECK(rcv_pdesc.type == MACH_MSG_PORT_DESCRIPTOR, "Descriptor is PORT_DESCRIPTOR");
    TEST_CHECK(rcv_pdesc.name != kMachPortNull, "Receiver was granted a valid port name");
    TEST_CHECK(rcv_space->has_right(rcv_pdesc.name, kMachPortRightSend), "Receiver possesses SEND right on transferred port");

    // Sender still retains its receive right
    TEST_CHECK(snd_space->has_right(my_port, kMachPortRightReceive), "Sender retains receive right after MAKE_SEND");

    std::cout << "  -> Port descriptor (MAKE_SEND) verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Port Descriptor with MACH_MSG_TYPE_MOVE_RECEIVE
// -----------------------------------------------------------------------------
void test_port_descriptor_move_receive() {
    std::cout << "[Stage 203.18 Test 2] Port Descriptor (MOVE_RECEIVE)...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 703;
    const std::uint32_t receiver_task = 704;

    auto snd_space = ipc.create_task_port_space(sender_task);
    auto rcv_space = ipc.create_task_port_space(receiver_task);

    mach_port_t rcv_dest = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    auto dest_obj = rcv_space->lookup_port(rcv_dest);
    mach_port_t snd_dest = snd_space->allocate_port(dest_obj, kMachPortRightSend);

    // Port to transfer
    mach_port_t move_port = ipc.mach_port_allocate(sender_task, kMachPortRightReceive);

    Memory mem(0x100000);
    const std::uint64_t snd_addr = 0x1000;

    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = MACH_MSGH_BITS_COMPLEX;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw) + sizeof(MachMsgPortDescriptorRaw);
    hdr.msgh_remote_port = snd_dest;
    hdr.msgh_local_port = kMachPortNull;

    MachMsgBodyRaw body{};
    body.msgh_descriptor_count = 1;

    MachMsgPortDescriptorRaw pdesc{};
    pdesc.name = move_port;
    pdesc.disposition = static_cast<std::uint8_t>(MACH_MSG_TYPE_MOVE_RECEIVE);
    pdesc.type = MACH_MSG_PORT_DESCRIPTOR;

    std::memcpy(mem.raw_data() + snd_addr, &hdr, sizeof(hdr));
    std::memcpy(mem.raw_data() + snd_addr + sizeof(hdr), &body, sizeof(body));
    std::memcpy(mem.raw_data() + snd_addr + sizeof(hdr) + sizeof(body), &pdesc, sizeof(pdesc));

    int sr = ipc.mach_msg(sender_task, snd_addr, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(sr == kMachMsgSuccess, "Send MOVE_RECEIVE succeeded");

    // Sender's receive right must be consumed
    TEST_CHECK(!snd_space->has_right(move_port, kMachPortRightReceive), "Sender no longer holds receive right");

    // Receiver receives
    const std::uint64_t rcv_addr = 0x4000;
    int rr = ipc.mach_msg(receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, rcv_dest, 0, mem);
    TEST_CHECK(rr == kMachMsgSuccess, "Receive MOVE_RECEIVE succeeded");

    MachMsgPortDescriptorRaw rcv_pdesc{};
    std::memcpy(&rcv_pdesc, mem.raw_data() + rcv_addr + sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw), sizeof(rcv_pdesc));
    TEST_CHECK(rcv_space->has_right(rcv_pdesc.name, kMachPortRightReceive), "Receiver has been granted RECEIVE right");

    std::cout << "  -> Port descriptor (MOVE_RECEIVE) verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Out-Of-Line (OOL) Memory Descriptor
// -----------------------------------------------------------------------------
void test_ool_memory_descriptor() {
    std::cout << "[Stage 203.18 Test 3] Out-Of-Line (OOL) Memory Descriptor...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 705;
    const std::uint32_t receiver_task = 706;

    auto snd_space = ipc.create_task_port_space(sender_task);
    auto rcv_space = ipc.create_task_port_space(receiver_task);

    mach_port_t rcv_dest = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    auto dest_obj = rcv_space->lookup_port(rcv_dest);
    mach_port_t snd_dest = snd_space->allocate_port(dest_obj, kMachPortRightSend);

    Memory mem(0x100000);

    // Prepare OOL payload at 0x30000
    const std::uint64_t ool_src_addr = 0x30000;
    std::uint8_t ool_data[128];
    for (std::size_t i = 0; i < sizeof(ool_data); ++i) {
        ool_data[i] = static_cast<std::uint8_t>((i ^ 0x5C) & 0xFF);
    }
    std::memcpy(mem.raw_data() + ool_src_addr, ool_data, sizeof(ool_data));

    // Construct complex message with OOL descriptor
    const std::uint64_t snd_addr = 0x1000;
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = MACH_MSGH_BITS_COMPLEX;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw) + sizeof(MachMsgOolDescriptorRaw);
    hdr.msgh_remote_port = snd_dest;

    MachMsgBodyRaw body{};
    body.msgh_descriptor_count = 1;

    MachMsgOolDescriptorRaw odesc{};
    odesc.address = ool_src_addr;
    odesc.size = sizeof(ool_data);
    odesc.deallocate = 1; // Deallocate in sender
    odesc.copy = 0;
    odesc.type = MACH_MSG_OOL_DESCRIPTOR;

    std::memcpy(mem.raw_data() + snd_addr, &hdr, sizeof(hdr));
    std::memcpy(mem.raw_data() + snd_addr + sizeof(hdr), &body, sizeof(body));
    std::memcpy(mem.raw_data() + snd_addr + sizeof(hdr) + sizeof(body), &odesc, sizeof(odesc));

    int sr = ipc.mach_msg(sender_task, snd_addr, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(sr == kMachMsgSuccess, "Send OOL message succeeded");

    // Sender's memory must be deallocated/cleared
    bool cleared = true;
    for (std::size_t i = 0; i < sizeof(ool_data); ++i) {
        if (mem.raw_data()[ool_src_addr + i] != 0) {
            cleared = false;
            break;
        }
    }
    TEST_CHECK(cleared, "Sender memory deallocated/zeroed on deallocate=1");

    // Receive message
    const std::uint64_t rcv_addr = 0x4000;
    int rr = ipc.mach_msg(receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, rcv_dest, 0, mem);
    TEST_CHECK(rr == kMachMsgSuccess, "Receive OOL message succeeded");

    MachMsgOolDescriptorRaw rcv_odesc{};
    std::memcpy(&rcv_odesc, mem.raw_data() + rcv_addr + sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw), sizeof(rcv_odesc));
    TEST_CHECK(rcv_odesc.type == MACH_MSG_OOL_DESCRIPTOR, "Descriptor is OOL_DESCRIPTOR");
    TEST_CHECK(rcv_odesc.size == sizeof(ool_data), "Size matches original payload");
    TEST_CHECK(rcv_odesc.address != 0, "Receiver address allocated");

    // Verify received OOL bytes
    std::uint8_t rcv_data[128]{0};
    std::memcpy(rcv_data, mem.raw_data() + rcv_odesc.address, sizeof(rcv_data));
    TEST_CHECK(std::memcmp(rcv_data, ool_data, sizeof(ool_data)) == 0, "OOL payload matches original bit-exact");

    std::cout << "  -> Out-of-line memory descriptor verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Mixed Complex Message (Port + OOL + Trailer Payload)
// -----------------------------------------------------------------------------
void test_mixed_complex_message() {
    std::cout << "[Stage 203.18 Test 4] Mixed Complex Message (Port + OOL + Trailer)...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 707;
    const std::uint32_t receiver_task = 708;

    auto snd_space = ipc.create_task_port_space(sender_task);
    auto rcv_space = ipc.create_task_port_space(receiver_task);

    mach_port_t rcv_dest = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    auto dest_obj = rcv_space->lookup_port(rcv_dest);
    mach_port_t snd_dest = snd_space->allocate_port(dest_obj, kMachPortRightSend);

    mach_port_t send_right_port = ipc.mach_port_allocate(sender_task, kMachPortRightReceive);

    Memory mem(0x100000);

    const std::uint64_t ool_addr = 0x38000;
    std::uint8_t ool_chunk[64]{0x77};
    std::memcpy(mem.raw_data() + ool_addr, ool_chunk, sizeof(ool_chunk));

    std::uint32_t trailer_magic = 0xBEEFCAFE;

    const std::uint64_t snd_addr = 0x1000;
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = MACH_MSGH_BITS_COMPLEX;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw) +
                    sizeof(MachMsgPortDescriptorRaw) + sizeof(MachMsgOolDescriptorRaw) + sizeof(trailer_magic);
    hdr.msgh_remote_port = snd_dest;

    MachMsgBodyRaw body{};
    body.msgh_descriptor_count = 2;

    MachMsgPortDescriptorRaw pdesc{};
    pdesc.name = send_right_port;
    pdesc.disposition = static_cast<std::uint8_t>(MACH_MSG_TYPE_MAKE_SEND);
    pdesc.type = MACH_MSG_PORT_DESCRIPTOR;

    MachMsgOolDescriptorRaw odesc{};
    odesc.address = ool_addr;
    odesc.size = sizeof(ool_chunk);
    odesc.deallocate = 0;
    odesc.copy = 0;
    odesc.type = MACH_MSG_OOL_DESCRIPTOR;

    std::size_t off = snd_addr;
    std::memcpy(mem.raw_data() + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
    std::memcpy(mem.raw_data() + off, &body, sizeof(body)); off += sizeof(body);
    std::memcpy(mem.raw_data() + off, &pdesc, sizeof(pdesc)); off += sizeof(pdesc);
    std::memcpy(mem.raw_data() + off, &odesc, sizeof(odesc)); off += sizeof(odesc);
    std::memcpy(mem.raw_data() + off, &trailer_magic, sizeof(trailer_magic)); off += sizeof(trailer_magic);

    int sr = ipc.mach_msg(sender_task, snd_addr, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(sr == kMachMsgSuccess, "Send mixed complex message succeeded");

    const std::uint64_t rcv_addr = 0x4000;
    int rr = ipc.mach_msg(receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, rcv_dest, 0, mem);
    TEST_CHECK(rr == kMachMsgSuccess, "Receive mixed complex message succeeded");

    MachMsgBodyRaw rcv_body{};
    std::memcpy(&rcv_body, mem.raw_data() + rcv_addr + sizeof(MachMsgHeaderRaw), sizeof(rcv_body));
    TEST_CHECK(rcv_body.msgh_descriptor_count == 2, "Body has 2 descriptors");

    std::uint32_t rcv_trailer = 0;
    std::size_t trailer_off = rcv_addr + sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw) +
                              sizeof(MachMsgPortDescriptorRaw) + sizeof(MachMsgOolDescriptorRaw);
    std::memcpy(&rcv_trailer, mem.raw_data() + trailer_off, sizeof(rcv_trailer));
    TEST_CHECK(rcv_trailer == trailer_magic, "Inline trailer magic preserved across complex transfer");

    std::cout << "  -> Mixed complex message verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.18: Mach Message Descriptors & Complex Messages Tests\n";
    std::cout << "==================================================================\n";

    test_port_descriptor_make_send();
    test_port_descriptor_move_receive();
    test_ool_memory_descriptor();
    test_mixed_complex_message();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.18 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
