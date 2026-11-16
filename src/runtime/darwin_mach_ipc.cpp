#include "runtime/darwin_mach_ipc.h"

#include <cstring>
#include <stdexcept>

namespace winios::runtime {

namespace {

// Maps virtual address into guest memory offset, handling high Darwin userspace stack windows
bool vaddr_to_offset(std::uint64_t vaddr, std::size_t size, const Memory& memory, std::size_t& out_offset) {
    if (vaddr < memory.size()) {
        out_offset = static_cast<std::size_t>(vaddr);
    } else if (vaddr >= 0x0000700000000000ULL && vaddr <= 0x00007FFFFFFFFFFFULL) {
        constexpr std::uint64_t kStackTop = 0x00007FFFFFE00000ULL;
        if (vaddr <= kStackTop) {
            std::uint64_t dist = kStackTop - vaddr;
            if (dist < memory.size()) {
                out_offset = memory.size() - static_cast<std::size_t>(dist);
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }
    return (out_offset + size <= memory.size());
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// MachPortObject Implementation
// -----------------------------------------------------------------------------
MachPortObject::MachPortObject(std::uint64_t id, std::uint32_t receiver_task)
    : id_(id), receiver_task_(receiver_task) {}

bool MachPortObject::enqueue_message(MachMessage msg) {
    std::lock_guard<std::mutex> guard(lock_);
    if (is_dead_) {
        return false;
    }
    if (queue_.size() >= kMaxQueueLimit) {
        return false;
    }
    queue_.push_back(std::move(msg));
    return true;
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

void MachPortObject::clear_queue() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    queue_.clear();
}

// -----------------------------------------------------------------------------
// MachPortSpace Implementation
// -----------------------------------------------------------------------------
MachPortSpace::MachPortSpace(std::uint32_t task_id)
    : task_id_(task_id) {
    slots_.resize(1); // slot 0 reserved for null/invalid

    // Initialize bootstrap port (service registry)
    auto bstrap_obj = std::make_shared<MachPortObject>(500, 1); // launchd task 1
    bootstrap_port_ = allocate_port(bstrap_obj, kMachPortRightSend);

    // Initialize task-self port (receive right for current task)
    auto self_obj = std::make_shared<MachPortObject>(1000 + task_id, task_id);
    task_self_ = allocate_port(self_obj, kMachPortRightReceive);

    // Initialize host-self port (send right to host kernel object)
    auto host_obj = std::make_shared<MachPortObject>(999, 0);
    host_self_ = allocate_port(host_obj, kMachPortRightSend);
}

mach_port_t MachPortSpace::allocate_port(std::shared_ptr<MachPortObject> object, std::uint32_t right) {
    std::lock_guard<std::mutex> guard(lock_);
    std::uint16_t idx = 0;
    if (!free_list_.empty()) {
        idx = free_list_.back();
        free_list_.pop_back();
        auto& entry = slots_[idx];
        entry.generation++;
        if (entry.generation == 0) entry.generation = 1;
        entry.object = std::move(object);
        entry.right_type = right;
        entry.urefs = 1;
        entry.in_use = true;
        return make_generation_handle(idx, entry.generation);
    }

    if (slots_.empty()) {
        slots_.resize(1);
    }
    idx = static_cast<std::uint16_t>(slots_.size());
    MachPortTableEntry entry;
    entry.object = std::move(object);
    entry.right_type = right;
    entry.urefs = 1;
    entry.generation = 1;
    entry.in_use = true;
    slots_.push_back(std::move(entry));
    return make_generation_handle(idx, 1);
}

bool MachPortSpace::insert_right(mach_port_t name, std::shared_ptr<MachPortObject> object, std::uint32_t right) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);

    if (idx > 0 && idx < slots_.size()) {
        auto& entry = slots_[idx];
        if (entry.in_use && entry.generation == gen) {
            entry.urefs++;
            entry.right_type = right;
            if (object) {
                entry.object = std::move(object);
            }
            return true;
        }
    }

    if (idx >= slots_.size()) {
        slots_.resize(idx + 1);
    }
    slots_[idx] = MachPortTableEntry{std::move(object), right, 1, (gen == 0 ? static_cast<std::uint16_t>(1) : gen), true};
    return true;
}

bool MachPortSpace::deallocate_port(mach_port_t name) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return false;

    auto& entry = slots_[idx];
    if (!entry.in_use || entry.generation != gen) return false;

    if (--entry.urefs == 0) {
        if (entry.right_type == kMachPortRightReceive && entry.object) {
            entry.object->mark_dead();
        }
        entry.in_use = false;
        entry.object.reset();
        free_list_.push_back(idx);
    }
    return true;
}

bool MachPortSpace::mod_refs(mach_port_t name, std::uint32_t right, std::int32_t delta) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return false;

    auto& entry = slots_[idx];
    if (!entry.in_use || entry.generation != gen) return false;

    if (entry.object && entry.object->is_dead()) {
        if (entry.right_type == kMachPortRightSend || entry.right_type == kMachPortRightSendOnce) {
            entry.right_type = kMachPortRightDeadName;
        }
    }

    if (entry.right_type != right) {
        return false;
    }

    if (delta < 0) {
        const auto udelta = static_cast<std::uint32_t>(-delta);
        if (udelta >= entry.urefs) {
            entry.urefs = 0;
            if (entry.right_type == kMachPortRightReceive && entry.object) {
                entry.object->mark_dead();
            }
            entry.in_use = false;
            entry.object.reset();
            free_list_.push_back(idx);
            return true;
        }
        entry.urefs -= udelta;
        return true;
    } else {
        entry.urefs += static_cast<std::uint32_t>(delta);
        return true;
    }
}

std::uint32_t MachPortSpace::get_refs(mach_port_t name, std::uint32_t right) const {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return 0;

    const auto& entry = slots_[idx];
    if (!entry.in_use || entry.generation != gen) return 0;

    if (entry.object && entry.object->is_dead()) {
        if (entry.right_type == kMachPortRightSend || entry.right_type == kMachPortRightSendOnce) {
            return (right == kMachPortRightDeadName) ? entry.urefs : 0;
        }
    }

    return (entry.right_type == right) ? entry.urefs : 0;
}

std::shared_ptr<MachPortObject> MachPortSpace::lookup_port(mach_port_t name) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return nullptr;

    auto& entry = slots_[idx];
    if (!entry.in_use || entry.generation != gen) return nullptr;

    if (entry.object && entry.object->is_dead()) {
        if (entry.right_type == kMachPortRightSend || entry.right_type == kMachPortRightSendOnce) {
            entry.right_type = kMachPortRightDeadName;
        }
        return nullptr;
    }
    return entry.object;
}

bool MachPortSpace::has_right(mach_port_t name, std::uint32_t right) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return false;

    auto& entry = slots_[idx];
    if (!entry.in_use || entry.generation != gen) return false;

    if (entry.object && entry.object->is_dead()) {
        if (entry.right_type == kMachPortRightSend || entry.right_type == kMachPortRightSendOnce) {
            entry.right_type = kMachPortRightDeadName;
        }
    }
    return entry.right_type == right;
}

std::uint32_t MachPortSpace::get_right_type(mach_port_t name) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return ~0u;

    auto& entry = slots_[idx];
    if (!entry.in_use || entry.generation != gen) return ~0u;

    if (entry.object && entry.object->is_dead()) {
        if (entry.right_type == kMachPortRightSend || entry.right_type == kMachPortRightSendOnce) {
            entry.right_type = kMachPortRightDeadName;
        }
    }
    return entry.right_type;
}

bool MachPortSpace::consume_send_once(mach_port_t name) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return false;

    auto& entry = slots_[idx];
    if (!entry.in_use || entry.generation != gen) return false;

    if (entry.right_type == kMachPortRightSendOnce) {
        entry.in_use = false;
        entry.object.reset();
        free_list_.push_back(idx);
        return true;
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

mach_port_t MachIpcSubsystem::mach_port_allocate(std::uint32_t task_id, std::uint32_t right) {
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
    return space->allocate_port(obj, right);
}

bool MachIpcSubsystem::mach_port_deallocate(std::uint32_t task_id, mach_port_t port) {
    auto space = get_task_port_space(task_id);
    if (!space) return false;
    return space->deallocate_port(port);
}

bool MachIpcSubsystem::mach_port_mod_refs(std::uint32_t task_id, mach_port_t port, std::uint32_t right, std::int32_t delta) {
    auto space = get_task_port_space(task_id);
    if (!space) return false;
    return space->mod_refs(port, right, delta);
}

std::uint32_t MachIpcSubsystem::mach_port_get_refs(std::uint32_t task_id, mach_port_t port, std::uint32_t right) {
    auto space = get_task_port_space(task_id);
    if (!space) return 0;
    return space->get_refs(port, right);
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
    return mach_msg_overwrite(
        task_id, msg_vaddr, option, send_size, rcv_size, rcv_name, timeout_ms, memory, msg_vaddr, rcv_size);
}

std::int32_t MachIpcSubsystem::mach_msg_overwrite(
    std::uint32_t task_id,
    std::uint64_t msg_vaddr,
    std::uint32_t option,
    std::uint32_t send_size,
    std::uint32_t rcv_size,
    mach_port_t rcv_name,
    std::uint32_t timeout_ms,
    Memory& memory,
    std::uint64_t rcv_msg_vaddr,
    std::uint32_t rcv_msg_size) {

    (void)timeout_ms;

    auto space = get_task_port_space(task_id);
    if (!space) return kMachSendInvalidDest;

    // 1. Handle MACH_SEND_MSG
    if ((option & kMachSendMsg) != 0) {
        if (send_size < sizeof(MachMsgHeaderRaw)) {
            return kMachSendInvalidData;
        }

        std::size_t send_off = 0;
        if (!vaddr_to_offset(msg_vaddr, send_size, memory, send_off)) {
            return kMachSendInvalidData;
        }

        MachMsgHeaderRaw hdr{};
        const auto* raw_ptr = memory.raw_data() + send_off;
        std::memcpy(&hdr, raw_ptr, sizeof(hdr));

        auto dest_obj = space->lookup_port(hdr.msgh_remote_port);
        if (!dest_obj) {
            return kMachSendInvalidDest;
        }

        auto right_type = space->get_right_type(hdr.msgh_remote_port);
        if (right_type == kMachPortRightDeadName) {
            return kMachSendInvalidDest;
        }
        if (right_type == kMachPortRightSendOnce) {
            space->consume_send_once(hdr.msgh_remote_port);
        }

        MachMessage msg;
        msg.header = hdr;
        if (hdr.msgh_local_port != kMachPortNull) {
            msg.reply_port_object = space->lookup_port(hdr.msgh_local_port);
        }

        const std::size_t payload_size = send_size - sizeof(MachMsgHeaderRaw);
        if (payload_size > 0) {
            msg.payload.resize(payload_size);
            std::memcpy(msg.payload.data(), raw_ptr + sizeof(MachMsgHeaderRaw), payload_size);
        }

        if (!dest_obj->enqueue_message(std::move(msg))) {
            if (dest_obj->is_dead()) {
                return kMachSendInvalidDest;
            }
            return kMachSendTimedOut;
        }
    }

    // 2. Handle MACH_RCV_MSG
    if ((option & kMachRcvMsg) != 0) {
        const std::uint64_t target_rcv_vaddr = (rcv_msg_vaddr != 0) ? rcv_msg_vaddr : msg_vaddr;
        const std::uint32_t target_rcv_size  = (rcv_msg_size != 0) ? rcv_msg_size : rcv_size;

        if (target_rcv_size < sizeof(MachMsgHeaderRaw)) {
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

        const std::size_t total_msg_size = sizeof(MachMsgHeaderRaw) + dequeued->payload.size();
        if (target_rcv_size < total_msg_size) {
            return kMachRcvTooLarge;
        }

        std::size_t rcv_off = 0;
        if (!vaddr_to_offset(target_rcv_vaddr, total_msg_size, memory, rcv_off)) {
            return kMachRcvHeaderError;
        }

        auto hdr = dequeued->header;
        hdr.msgh_size = static_cast<std::uint32_t>(total_msg_size);

        if (dequeued->reply_port_object) {
            // Allocate a send right in the receiver's port space for the reply port
            mach_port_t receiver_remote_port = space->allocate_port(dequeued->reply_port_object, kMachPortRightSend);
            hdr.msgh_remote_port = receiver_remote_port;
            hdr.msgh_local_port = kMachPortNull;
        }

        std::memcpy(memory.raw_data() + rcv_off, &hdr, sizeof(hdr));

        if (!dequeued->payload.empty()) {
            std::memcpy(memory.raw_data() + rcv_off + sizeof(hdr),
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
        case -26: { // mach_reply_port (XNU syscall_sw.c: -26)
            out_retval = mach_reply_port(task_id);
            return 0;
        }
        case -27: { // thread_self_trap (XNU syscall_sw.c: -27)
            auto space = get_task_port_space(task_id);
            if (!space) space = create_task_port_space(task_id);
            auto obj = std::make_shared<MachPortObject>(3000 + task_id, task_id);
            out_retval = space->allocate_port(obj, kMachPortRightReceive);
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
        case -31: { // mach_msg_trap (XNU syscall_sw.c: -31)
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
        case -32: { // mach_msg_overwrite_trap (XNU syscall_sw.c: -32)
            if (args.size() < 6) return -1;
            std::uint64_t msg_vaddr     = args[0];
            auto          option        = static_cast<std::uint32_t>(args[1]);
            auto          send_size     = static_cast<std::uint32_t>(args[2]);
            auto          rcv_size      = static_cast<std::uint32_t>(args[3]);
            auto          rcv_name      = static_cast<mach_port_t>(args[4]);
            auto          timeout_ms    = static_cast<std::uint32_t>(args[5]);
            std::uint64_t rcv_msg_vaddr = (args.size() > 7) ? args[7] : 0;
            auto          rcv_msg_size  = (args.size() > 8) ? static_cast<std::uint32_t>(args[8]) : 0;

            out_retval = static_cast<std::uint64_t>(
                mach_msg_overwrite(task_id, msg_vaddr, option, send_size, rcv_size, rcv_name, timeout_ms, memory, rcv_msg_vaddr, rcv_msg_size));
            return 0;
        }
        case -61: { // mach_port_allocate
            if (args.size() < 2) return -1;
            auto right = static_cast<std::uint32_t>(args[1]);
            out_retval = mach_port_allocate(task_id, right);
            return 0;
        }
        case -62: { // mach_port_deallocate
            if (args.size() < 2) return -1;
            auto port = static_cast<mach_port_t>(args[1]);
            out_retval = mach_port_deallocate(task_id, port) ? 0 : 1;
            return 0;
        }
        case -63: { // mach_port_mod_refs
            if (args.size() < 4) return -1;
            auto port = static_cast<mach_port_t>(args[1]);
            auto right = static_cast<std::uint32_t>(args[2]);
            auto delta = static_cast<std::int32_t>(args[3]);
            out_retval = mach_port_mod_refs(task_id, port, right, delta) ? 0 : 1;
            return 0;
        }
        case -64: { // mach_port_get_refs
            if (args.size() < 3) return -1;
            auto port = static_cast<mach_port_t>(args[1]);
            auto right = static_cast<std::uint32_t>(args[2]);
            out_retval = mach_port_get_refs(task_id, port, right);
            return 0;
        }
        default:
            break;
    }

    return -1; // Unhandled Mach trap
}

} // namespace winios::runtime
