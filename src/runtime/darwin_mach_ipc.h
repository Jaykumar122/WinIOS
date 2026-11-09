#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "memory/memory.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Mach IPC Standard Definitions & Constants
// -----------------------------------------------------------------------------
using mach_port_t = std::uint32_t;

inline constexpr mach_port_t kMachPortNull = 0u;
inline constexpr mach_port_t kMachPortDead = ~0u;

// Port Rights
inline constexpr std::uint32_t kMachPortRightSend      = 0u;
inline constexpr std::uint32_t kMachPortRightReceive   = 1u;
inline constexpr std::uint32_t kMachPortRightSendOnce  = 2u;
inline constexpr std::uint32_t kMachPortRightPortSet   = 3u;
inline constexpr std::uint32_t kMachPortRightDeadName  = 4u;

// mach_msg Option Flags
inline constexpr std::uint32_t kMachSendMsg            = 0x00000001u;
inline constexpr std::uint32_t kMachRcvMsg             = 0x00000002u;
inline constexpr std::uint32_t kMachSendTimeout        = 0x00000010u;
inline constexpr std::uint32_t kMachRcvTimeout         = 0x00000100u;
inline constexpr std::uint32_t kMachSendInterrupt      = 0x00000040u;
inline constexpr std::uint32_t kMachRcvInterrupt       = 0x00000400u;

// mach_msg Return Codes (mach_msg_return_t)
inline constexpr std::int32_t  kMachMsgSuccess         = 0;
inline constexpr std::int32_t  kMachSendInvalidData    = 0x10000002;
inline constexpr std::int32_t  kMachSendInvalidDest    = 0x10000003;
inline constexpr std::int32_t  kMachSendTimedOut       = 0x10000004;
inline constexpr std::int32_t  kMachRcvInvalidName     = 0x10004002;
inline constexpr std::int32_t  kMachRcvTimedOut        = 0x10004003;
inline constexpr std::int32_t  kMachRcvTooLarge        = 0x10004004;
inline constexpr std::int32_t  kMachRcvHeaderError     = 0x1000400B;

#pragma pack(push, 1)
struct MachMsgHeaderRaw {
    std::uint32_t msgh_bits{0};
    std::uint32_t msgh_size{0};
    mach_port_t   msgh_remote_port{0};
    mach_port_t   msgh_local_port{0};
    mach_port_t   msgh_voucher_port{0};
    std::int32_t  msgh_id{0};
};

inline constexpr std::uint32_t MACH_MSGH_BITS_COMPLEX         = 0x80000000u;

inline constexpr std::uint32_t MACH_MSG_PORT_DESCRIPTOR        = 0;
inline constexpr std::uint32_t MACH_MSG_OOL_DESCRIPTOR         = 1;
inline constexpr std::uint32_t MACH_MSG_OOL_PORTS_DESCRIPTOR   = 2;
inline constexpr std::uint32_t MACH_MSG_OOL_VOLATILE_DESCRIPTOR= 3;

inline constexpr std::uint32_t MACH_MSG_TYPE_MOVE_RECEIVE      = 16;
inline constexpr std::uint32_t MACH_MSG_TYPE_MOVE_SEND         = 17;
inline constexpr std::uint32_t MACH_MSG_TYPE_MOVE_SEND_ONCE    = 18;
inline constexpr std::uint32_t MACH_MSG_TYPE_COPY_SEND         = 19;
inline constexpr std::uint32_t MACH_MSG_TYPE_MAKE_SEND         = 20;
inline constexpr std::uint32_t MACH_MSG_TYPE_MAKE_SEND_ONCE    = 21;

struct MachMsgBodyRaw {
    std::uint32_t msgh_descriptor_count{0};
};

struct MachMsgPortDescriptorRaw {
    mach_port_t   name{0};
    std::uint32_t pad1{0};
    std::uint16_t pad2{0};
    std::uint8_t  disposition{0};
    std::uint8_t  type{MACH_MSG_PORT_DESCRIPTOR};
};

struct MachMsgOolDescriptorRaw {
    std::uint64_t address{0};
    std::uint32_t size{0};
    std::uint8_t  deallocate{0};
    std::uint8_t  copy{0};
    std::uint8_t  pad1{0};
    std::uint8_t  type{MACH_MSG_OOL_DESCRIPTOR};
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Mach IPC In-Memory Message Representation
// -----------------------------------------------------------------------------
class MachPortObject;

struct MachMessage {
    MachMsgHeaderRaw                header{};
    std::shared_ptr<MachPortObject> reply_port_object;
    std::vector<std::uint8_t>       payload;
};

// -----------------------------------------------------------------------------
// Mach Port Kernel Object & Port Space
// -----------------------------------------------------------------------------
class MachPortObject {
public:
    explicit MachPortObject(std::uint64_t id, std::uint32_t receiver_task = 0);
    ~MachPortObject() = default;

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::uint32_t receiver_task() const noexcept { return receiver_task_; }
    void set_receiver_task(std::uint32_t task_id) noexcept { receiver_task_ = task_id; }

    [[nodiscard]] const std::string& service_name() const noexcept { return service_name_; }
    void set_service_name(std::string name) { service_name_ = std::move(name); }

    // Enqueue & Dequeue operations
    void enqueue_message(MachMessage msg);
    [[nodiscard]] std::optional<MachMessage> dequeue_message();
    [[nodiscard]] bool has_messages() const noexcept;
    [[nodiscard]] std::size_t queue_size() const noexcept;

private:
    std::uint64_t            id_{0};
    std::uint32_t            receiver_task_{0};
    std::string              service_name_;
    mutable std::mutex       lock_;
    std::deque<MachMessage>  queue_;
};

struct MachPortRightEntry {
    std::shared_ptr<MachPortObject> object;
    std::uint32_t                   right_type{kMachPortRightSend};
    std::uint32_t                   urefs{1};
};

class MachPortSpace {
public:
    explicit MachPortSpace(std::uint32_t task_id);
    ~MachPortSpace() = default;

    [[nodiscard]] std::uint32_t task_id() const noexcept { return task_id_; }

    // Port allocation
    mach_port_t allocate_port(std::shared_ptr<MachPortObject> object, std::uint32_t right);
    bool insert_right(mach_port_t name, std::shared_ptr<MachPortObject> object, std::uint32_t right);
    bool deallocate_port(mach_port_t name);

    [[nodiscard]] std::shared_ptr<MachPortObject> lookup_port(mach_port_t name) const;
    [[nodiscard]] bool has_right(mach_port_t name, std::uint32_t right) const;

    // Standard task rights
    [[nodiscard]] mach_port_t task_self() const noexcept { return task_self_; }
    [[nodiscard]] mach_port_t host_self() const noexcept { return host_self_; }
    [[nodiscard]] mach_port_t bootstrap_port() const noexcept { return bootstrap_port_; }
    void set_bootstrap_port(mach_port_t port) noexcept { bootstrap_port_ = port; }

private:
    std::uint32_t                                      task_id_{0};
    mach_port_t                                        next_name_{0x103};
    mach_port_t                                        task_self_{0x101};
    mach_port_t                                        host_self_{0x102};
    mach_port_t                                        bootstrap_port_{0x100};
    mutable std::mutex                                 lock_;
    std::unordered_map<mach_port_t, MachPortRightEntry> table_;
};

// -----------------------------------------------------------------------------
// MachIpcSubsystem: Global Host & Userspace IPC Coordinator
// -----------------------------------------------------------------------------
class MachIpcSubsystem {
public:
    MachIpcSubsystem();
    ~MachIpcSubsystem() = default;

    // Task Space Management
    std::shared_ptr<MachPortSpace> create_task_port_space(std::uint32_t task_id);
    std::shared_ptr<MachPortSpace> get_task_port_space(std::uint32_t task_id);
    void destroy_task_port_space(std::uint32_t task_id);

    // Global Bootstrap Server Registry (launchd service catalog)
    bool bootstrap_register(std::uint32_t task_id, const std::string& name, mach_port_t port);
    [[nodiscard]] mach_port_t bootstrap_look_up(std::uint32_t task_id, const std::string& name);

    // Primary Mach Trap: mach_msg
    [[nodiscard]] std::int32_t mach_msg(
        std::uint32_t task_id,
        std::uint64_t msg_vaddr,
        std::uint32_t option,
        std::uint32_t send_size,
        std::uint32_t rcv_size,
        mach_port_t rcv_name,
        std::uint32_t timeout_ms,
        Memory& memory);

    // Trap Dispatch Helpers
    [[nodiscard]] mach_port_t mach_reply_port(std::uint32_t task_id);
    [[nodiscard]] mach_port_t mach_task_self(std::uint32_t task_id);
    [[nodiscard]] mach_port_t mach_host_self(std::uint32_t task_id);

    [[nodiscard]] std::int32_t dispatch_mach_trap(
        std::uint32_t task_id,
        std::int64_t trap_no,
        const std::vector<std::uint64_t>& args,
        Memory& memory,
        std::uint64_t& out_retval);

private:
    std::uint64_t                                      next_object_id_{1};
    mutable std::mutex                                 lock_;
    std::unordered_map<std::uint32_t, std::shared_ptr<MachPortSpace>> task_spaces_;
    std::unordered_map<std::string, std::shared_ptr<MachPortObject>> bootstrap_services_;
};

} // namespace winios::runtime
