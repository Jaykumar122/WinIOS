#include "runtime/darwin_mach_ipc.h"

#include <cstring>
#include <stdexcept>

namespace winios::runtime {

// -----------------------------------------------------------------------------
// MachPortObject Implementation
// -----------------------------------------------------------------------------
MachPortObject::MachPortObject(std::uint64_t id, std::uint32_t receiver_task)
    : id_(id), receiver_task_(receiver_task) {}

void MachPortObject::enqueue_message(MachMessage msg) {
    std::lock_guard<std::mutex> guard(lock_);
    queue_.push_back(std::move(msg));
}

std::optional<MachMessage> MachPortObject::dequeue_message() {
    std::lock_guard<std::mutex> guard(lock_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    MachMessage msg = std::move(queue_.front());
    queue_.pop_front();
    return msg;
}

bool MachPortObject::has_messages() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return !queue_.empty();
}

std::size_t MachPortObject::queue_size() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return queue_.size();
}

// -----------------------------------------------------------------------------
// MachPortSpace Implementation
// -----------------------------------------------------------------------------
MachPortSpace::MachPortSpace(std::uint32_t task_id)
    : task_id_(task_id) {

    // Initialize task-self and host-self ports
    auto self_obj = std::make_shared<MachPortObject>(1000 + task_id, task_id);
    table_[task_self_] = MachPortRightEntry{self_obj, kMachPortRightReceive, 1};

    auto host_obj = std::make_shared<MachPortObject>(999, 0);
    table_[host_self_] = MachPortRightEntry{host_obj, kMachPortRightSend, 1};

    auto bstrap_obj = std::make_shared<MachPortObject>(500, 1); // launchd task 1
    table_[bootstrap_port_] = MachPortRightEntry{bstrap_obj, kMachPortRightSend, 1};
}

mach_port_t MachPortSpace::allocate_port(std::shared_ptr<MachPortObject> object, std::uint32_t right) {
    std::lock_guard<std::mutex> guard(lock_);
    mach_port_t name = next_name_++;
    table_[name] = MachPortRightEntry{std::move(object), right, 1};
    return name;
}

bool MachPortSpace::insert_right(mach_port_t name, std::shared_ptr<MachPortObject> object, std::uint32_t right) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = table_.find(name);
    if (it != table_.end()) {
        it->second.urefs++;
        it->second.right_type = right;
        return true;
    }
    table_[name] = MachPortRightEntry{std::move(object), right, 1};
    return true;
}

bool MachPortSpace::deallocate_port(mach_port_t name) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = table_.find(name);
    if (it == table_.end()) {
        return false;
    }
    if (--it->second.urefs == 0) {
        table_.erase(it);
    }
    return true;
}

std::shared_ptr<MachPortObject> MachPortSpace::lookup_port(mach_port_t name) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = table_.find(name);
    if (it != table_.end()) {
        return it->second.object;
    }
    return nullptr;
}

bool MachPortSpace::has_right(mach_port_t name, std::uint32_t right) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = table_.find(name);
    if (it != table_.end()) {
        return it->second.right_type == right;
    }
    return false;
}

// -----------------------------------------------------------------------------
// MachIpcSubsystem Implementation
// -----------------------------------------------------------------------------
MachIpcSubsystem::MachIpcSubsystem() {
    // Task 1 is /sbin/launchd (Bootstrap server)
    create_task_port_space(1);
}

std::shared_ptr<MachPortSpace> MachIpcSubsystem::create_task_port_space(std::uint32_t task_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto space = std::make_shared<MachPortSpace>(task_id);
    task_spaces_[task_id] = space;
    return space;
}

std::shared_ptr<MachPortSpace> MachIpcSubsystem::get_task_port_space(std::uint32_t task_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = task_spaces_.find(task_id);
    if (it != task_spaces_.end()) {
        return it->second;
    }
    return nullptr;
}

void MachIpcSubsystem::destroy_task_port_space(std::uint32_t task_id) {
    std::lock_guard<std::mutex> guard(lock_);
    task_spaces_.erase(task_id);
}

bool MachIpcSubsystem::bootstrap_register(std::uint32_t task_id, const std::string& name, mach_port_t port) {
    auto space = get_task_port_space(task_id);
    if (!space) return false;

    auto obj = space->lookup_port(port);
    if (!obj) return false;

    std::lock_guard<std::mutex> guard(lock_);
    obj->set_service_name(name);
    bootstrap_services_[name] = obj;
    return true;
}

mach_port_t MachIpcSubsystem::bootstrap_look_up(std::uint32_t task_id, const std::string& name) {
    std::shared_ptr<MachPortObject> obj;
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto it = bootstrap_services_.find(name);
        if (it != bootstrap_services_.end()) {
            obj = it->second;
        }
    }

    if (!obj) {
        return kMachPortNull;
    }

    auto space = get_task_port_space(task_id);
    if (!space) {
        space = create_task_port_space(task_id);
    }

    // Allocate a send right in the client's port space
    return space->allocate_port(obj, kMachPortRightSend);
}

mach_port_t MachIpcSubsystem::mach_reply_port(std::uint32_t task_id) {
    auto space = get_task_port_space(task_id);
    if (!space) {
        space = create_task_port_space(task_id);
    }

    std::uint64_t oid = 0;
    {
        std::lock_guard<std::mutex> guard(lock_);
        oid = next_object_id_++;
    }
    auto obj = std::make_shared<MachPortObject>(oid, task_id);
    return space->allocate_port(obj, kMachPortRightReceive);
}

mach_port_t MachIpcSubsystem::mach_task_self(std::uint32_t task_id) {
    auto space = get_task_port_space(task_id);
    return space ? space->task_self() : kMachPortNull;
}

mach_port_t MachIpcSubsystem::mach_host_self(std::uint32_t task_id) {
    auto space = get_task_port_space(task_id);
    return space ? space->host_self() : kMachPortNull;
}

std::int32_t MachIpcSubsystem::mach_msg(
    std::uint32_t task_id,
    std::uint64_t msg_vaddr,
    std::uint32_t option,
    std::uint32_t send_size,
    std::uint32_t rcv_size,
    mach_port_t rcv_name,
    std::uint32_t timeout_ms,
    Memory& memory) {

    (void)timeout_ms;

    auto space = get_task_port_space(task_id);
    if (!space) return kMachSendInvalidDest;

    // 1. Handle MACH_SEND_MSG
    if ((option & kMachSendMsg) != 0) {
        if (send_size < sizeof(MachMsgHeaderRaw)) {
            return kMachSendInvalidData;
        }

        std::size_t off = 0;
        if (msg_vaddr < memory.size()) {
            off = static_cast<std::size_t>(msg_vaddr);
        } else if (msg_vaddr >= 0x0000700000000000ULL && msg_vaddr <= 0x00007FFFFFFFFFFFULL) {
            std::uint64_t dist = 0x00007FFFFFE00000ULL - msg_vaddr;
            if (dist < memory.size()) {
                off = memory.size() - static_cast<std::size_t>(dist);
            } else {
                return kMachSendInvalidData;
            }
        } else {
            return kMachSendInvalidData;
        }
        if (off + send_size > memory.size()) {
            return kMachSendInvalidData;
        }

        MachMsgHeaderRaw hdr{};
        const auto* raw_ptr = memory.raw_data() + off;
        std::memcpy(&hdr, raw_ptr, sizeof(hdr));

        auto dest_obj = space->lookup_port(hdr.msgh_remote_port);
        if (!dest_obj) {
            return kMachSendInvalidDest;
        }

        MachMessage msg;
        msg.header = hdr;
        if (hdr.msgh_local_port != kMachPortNull) {
            msg.reply_port_object = space->lookup_port(hdr.msgh_local_port);
        }

        std::size_t payload_size = send_size - sizeof(MachMsgHeaderRaw);
        if (payload_size > 0) {
            msg.payload.resize(payload_size);
            std::memcpy(msg.payload.data(), raw_ptr + sizeof(MachMsgHeaderRaw), payload_size);
        }

        dest_obj->enqueue_message(std::move(msg));
    }

    // 2. Handle MACH_RCV_MSG
    if ((option & kMachRcvMsg) != 0) {
        if (rcv_size < sizeof(MachMsgHeaderRaw)) {
            return kMachRcvHeaderError;
        }

        auto rcv_obj = space->lookup_port(rcv_name);
        if (!rcv_obj) {
            return kMachRcvInvalidName;
        }

        auto dequeued = rcv_obj->dequeue_message();
        if (!dequeued.has_value()) {
            return kMachRcvTimedOut;
        }

        std::size_t total_msg_size = sizeof(MachMsgHeaderRaw) + dequeued->payload.size();
        if (rcv_size < total_msg_size) {
            return kMachRcvTooLarge;
        }

        std::size_t off = 0;
        if (msg_vaddr < memory.size()) {
            off = static_cast<std::size_t>(msg_vaddr);
        } else if (msg_vaddr >= 0x0000700000000000ULL && msg_vaddr <= 0x00007FFFFFFFFFFFULL) {
            std::uint64_t dist = 0x00007FFFFFE00000ULL - msg_vaddr;
            if (dist < memory.size()) {
                off = memory.size() - static_cast<std::size_t>(dist);
            } else {
                return kMachRcvHeaderError;
            }
        } else {
            return kMachRcvHeaderError;
        }
        if (off + total_msg_size > memory.size()) {
            return kMachRcvHeaderError;
        }

        // Write header with port rights translation
        auto hdr = dequeued->header;
        hdr.msgh_size = static_cast<std::uint32_t>(total_msg_size);

        if (dequeued->reply_port_object) {
            // Allocate a send right in the receiver's port space for the reply port
            mach_port_t receiver_remote_port = space->allocate_port(dequeued->reply_port_object, kMachPortRightSend);
            hdr.msgh_remote_port = receiver_remote_port;
            hdr.msgh_local_port = kMachPortNull;
        }

        std::memcpy(memory.raw_data() + off, &hdr, sizeof(hdr));

        // Write payload
        if (!dequeued->payload.empty()) {
            std::memcpy(memory.raw_data() + off + sizeof(hdr),
                        dequeued->payload.data(),
                        dequeued->payload.size());
        }
    }

    return kMachMsgSuccess;
}

std::int32_t MachIpcSubsystem::dispatch_mach_trap(
    std::uint32_t task_id,
    std::int64_t trap_no,
    const std::vector<std::uint64_t>& args,
    Memory& memory,
    std::uint64_t& out_retval) {

    out_retval = 0;

    switch (trap_no) {
        case -26: // mach_msg
        case -31: {
            if (args.size() < 6) return -1;
            std::uint64_t msg_vaddr   = args[0];
            auto          option      = static_cast<std::uint32_t>(args[1]);
            auto          send_size   = static_cast<std::uint32_t>(args[2]);
            auto          rcv_size    = static_cast<std::uint32_t>(args[3]);
            auto          rcv_name    = static_cast<mach_port_t>(args[4]);
            auto          timeout_ms  = static_cast<std::uint32_t>(args[5]);

            out_retval = static_cast<std::uint64_t>(
                mach_msg(task_id, msg_vaddr, option, send_size, rcv_size, rcv_name, timeout_ms, memory));
            return 0;
        }
        case -27: { // mach_reply_port
            out_retval = mach_reply_port(task_id);
            return 0;
        }
        case -28: { // mach_task_self
            out_retval = mach_task_self(task_id);
            return 0;
        }
        case -29: { // mach_host_self
            out_retval = mach_host_self(task_id);
            return 0;
        }
        case -61: { // mach_port_allocate
            if (args.size() < 2) return -1;
            auto space = get_task_port_space(task_id);
            if (!space) return -1;

            auto right = static_cast<std::uint32_t>(args[1]);
            std::uint64_t oid = 0;
            {
                std::lock_guard<std::mutex> guard(lock_);
                oid = next_object_id_++;
            }
            auto obj = std::make_shared<MachPortObject>(oid, task_id);
            out_retval = space->allocate_port(obj, right);
            return 0;
        }
        default:
            break;
    }

    return -1; // Unhandled Mach trap
}

} // namespace winios::runtime
