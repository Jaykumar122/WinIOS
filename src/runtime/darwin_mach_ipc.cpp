#include "runtime/darwin_mach_ipc.h"

#include <algorithm>
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

void MachPortObject::add_dead_name_notification(DeadNameNotification notif) {
    std::lock_guard<std::mutex> guard(lock_);
    dead_name_notifications_.push_back(std::move(notif));
}

void MachPortObject::mark_dead() noexcept {
    std::vector<DeadNameNotification> notifs_to_fire;
    {
        std::lock_guard<std::mutex> guard(lock_);
        is_dead_ = true;
        notifs_to_fire = std::move(dead_name_notifications_);
    }
    for (const auto& notif : notifs_to_fire) {
        if (notif.notify_port_object) {
            MachMessage msg;
            msg.header.msgh_id = 72; // MACH_NOTIFY_DEAD_NAME
            msg.header.msgh_bits = 0;
            msg.header.msgh_size = static_cast<std::uint32_t>(sizeof(MachMsgHeaderRaw) + sizeof(mach_port_t));
            msg.header.msgh_remote_port = notif.notify_port_name;
            msg.header.msgh_local_port = kMachPortNull;
            msg.header.msgh_voucher_port = kMachPortNull;
            msg.payload.resize(sizeof(mach_port_t));
            std::memcpy(msg.payload.data(), &notif.monitored_name, sizeof(mach_port_t));
            (void)notif.notify_port_object->enqueue_message(std::move(msg));
        }
    }
}

// -----------------------------------------------------------------------------
// MachPortSpace Implementation
// -----------------------------------------------------------------------------
MachPortSpace::MachPortSpace(std::uint32_t task_id)
    : task_id_(task_id) {
    slots_.resize(0x104); // reserve up to 0x103

    // Initialize task-self port (0x101)
    task_self_ = 0x101;
    auto self_obj = std::make_shared<MachPortObject>(1000 + task_id, task_id);
    insert_right(task_self_, self_obj, kMachPortRightReceive);

    // Initialize host-self port (0x102)
    host_self_ = 0x102;
    auto host_obj = std::make_shared<MachPortObject>(999, 0);
    insert_right(host_self_, host_obj, kMachPortRightSend);

    // Initialize bootstrap port (service registry - 0x103)
    bootstrap_port_ = 0x103;
    auto bstrap_obj = std::make_shared<MachPortObject>(500, 1); // launchd task 1
    insert_right(bootstrap_port_, bstrap_obj, kMachPortRightSend);
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
        if (entry.in_use && (gen == 0 || entry.generation == gen)) {
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
    if (!entry.in_use || (gen != 0 && entry.generation != gen)) return false;

    if (--entry.urefs == 0) {
        if (entry.right_type == kMachPortRightPortSet) {
            auto it = port_sets_.find(name);
            if (it != port_sets_.end()) {
                for (mach_port_t m : it->second) {
                    member_to_set_.erase(m);
                }
                port_sets_.erase(it);
            }
        } else if (entry.right_type == kMachPortRightReceive) {
            auto it = member_to_set_.find(name);
            if (it != member_to_set_.end()) {
                auto& members = port_sets_[it->second];
                members.erase(std::remove(members.begin(), members.end(), name), members.end());
                member_to_set_.erase(it);
            }
            if (entry.object) {
                entry.object->mark_dead();
            }
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
    if (!entry.in_use || (gen != 0 && entry.generation != gen)) return false;

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
            if (entry.right_type == kMachPortRightPortSet) {
                auto it = port_sets_.find(name);
                if (it != port_sets_.end()) {
                    for (mach_port_t m : it->second) {
                        member_to_set_.erase(m);
                    }
                    port_sets_.erase(it);
                }
            } else if (entry.right_type == kMachPortRightReceive) {
                auto it = member_to_set_.find(name);
                if (it != member_to_set_.end()) {
                    auto& members = port_sets_[it->second];
                    members.erase(std::remove(members.begin(), members.end(), name), members.end());
                    member_to_set_.erase(it);
                }
                if (entry.object) {
                    entry.object->mark_dead();
                }
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
    if (!entry.in_use || (gen != 0 && entry.generation != gen)) return 0;

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
    if (!entry.in_use || (gen != 0 && entry.generation != gen)) return nullptr;

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
    if (!entry.in_use || (gen != 0 && entry.generation != gen)) return false;

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
    if (!entry.in_use || (gen != 0 && entry.generation != gen)) return ~0u;

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
    if (!entry.in_use || (gen != 0 && entry.generation != gen)) return false;

    if (entry.right_type == kMachPortRightSendOnce) {
        entry.in_use = false;
        entry.object.reset();
        free_list_.push_back(idx);
        return true;
    }
    return false;
}

bool MachPortSpace::is_port_set(mach_port_t name) const {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t idx = handle_index(name);
    const std::uint16_t gen = handle_generation(name);
    if (idx == 0 || idx >= slots_.size()) return false;
    const auto& entry = slots_[idx];
    return entry.in_use && (gen == 0 || entry.generation == gen) && entry.right_type == kMachPortRightPortSet;
}

std::vector<mach_port_t> MachPortSpace::get_set_members(mach_port_t set_name) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = port_sets_.find(set_name);
    if (it != port_sets_.end()) {
        return it->second;
    }
    return {};
}

bool MachPortSpace::insert_set_member(mach_port_t member, mach_port_t set) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint16_t set_idx = handle_index(set);
    const std::uint16_t set_gen = handle_generation(set);
    if (set_idx == 0 || set_idx >= slots_.size()) return false;
    if (!slots_[set_idx].in_use || (set_gen != 0 && slots_[set_idx].generation != set_gen) || slots_[set_idx].right_type != kMachPortRightPortSet) {
        return false;
    }

    const std::uint16_t mem_idx = handle_index(member);
    const std::uint16_t mem_gen = handle_generation(member);
    if (mem_idx == 0 || mem_idx >= slots_.size()) return false;
    if (!slots_[mem_idx].in_use || (mem_gen != 0 && slots_[mem_idx].generation != mem_gen) || slots_[mem_idx].right_type != kMachPortRightReceive) {
        return false;
    }

    // If member already in a set, remove it first
    auto prev_it = member_to_set_.find(member);
    if (prev_it != member_to_set_.end()) {
        mach_port_t prev_set = prev_it->second;
        auto& members = port_sets_[prev_set];
        members.erase(std::remove(members.begin(), members.end(), member), members.end());
        member_to_set_.erase(prev_it);
    }

    port_sets_[set].push_back(member);
    member_to_set_[member] = set;
    return true;
}

bool MachPortSpace::extract_set_member(mach_port_t member, mach_port_t set) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = member_to_set_.find(member);
    if (it == member_to_set_.end() || it->second != set) {
        return false;
    }

    member_to_set_.erase(it);
    auto& members = port_sets_[set];
    members.erase(std::remove(members.begin(), members.end(), member), members.end());
    return true;
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

bool MachIpcSubsystem::mach_port_insert_member(std::uint32_t task_id, mach_port_t member_port, mach_port_t set_port) {
    auto space = get_task_port_space(task_id);
    if (!space) return false;
    return space->insert_set_member(member_port, set_port);
}

bool MachIpcSubsystem::mach_port_extract_member(std::uint32_t task_id, mach_port_t member_port, mach_port_t set_port) {
    auto space = get_task_port_space(task_id);
    if (!space) return false;
    return space->extract_set_member(member_port, set_port);
}

bool MachIpcSubsystem::mach_port_request_notification(
    std::uint32_t task_id,
    mach_port_t name,
    std::uint32_t id,
    mach_port_t notify_port) {

    auto space = get_task_port_space(task_id);
    if (!space) return false;

    auto target_obj = space->lookup_port(name);
    if (!target_obj) return false;

    auto notify_obj = space->lookup_port(notify_port);
    if (!notify_obj) return false;

    if (id == MACH_NOTIFY_DEAD_NAME) {
        DeadNameNotification notif;
        notif.task_id = task_id;
        notif.notify_port_name = notify_port;
        notif.notify_port_object = notify_obj;
        notif.monitored_name = name;
        target_obj->add_dead_name_notification(std::move(notif));
        return true;
    }
    return false;
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
        if (dest_obj->is_dead() || right_type == kMachPortRightDeadName) {
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

        if ((hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX) != 0 && send_size >= sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw)) {
            msg.is_complex = true;
            MachMsgBodyRaw body{};
            std::memcpy(&body, raw_ptr + sizeof(MachMsgHeaderRaw), sizeof(body));

            std::size_t cur_off = sizeof(MachMsgHeaderRaw) + sizeof(MachMsgBodyRaw);
            for (std::uint32_t i = 0; i < body.msgh_descriptor_count; ++i) {
                if (cur_off >= send_size) break;
                if (cur_off + sizeof(MachMsgOolDescriptorRaw) <= send_size &&
                    raw_ptr[cur_off + 15] == MACH_MSG_OOL_DESCRIPTOR) {
                    MachMsgOolDescriptorRaw odesc{};
                    std::memcpy(&odesc, raw_ptr + cur_off, sizeof(odesc));
                    InFlightOolDescriptor ifod;
                    ifod.size = odesc.size;
                    ifod.deallocate = odesc.deallocate;
                    ifod.copy = odesc.copy;
                    ifod.data.resize(odesc.size);

                    std::size_t ool_off = 0;
                    if (vaddr_to_offset(odesc.address, odesc.size, memory, ool_off)) {
                        std::memcpy(ifod.data.data(), memory.raw_data() + ool_off, odesc.size);
                        if (odesc.deallocate != 0) {
                            std::memset(memory.raw_data() + ool_off, 0, odesc.size);
                        }
                    }
                    msg.ool_descriptors.push_back(std::move(ifod));
                    cur_off += sizeof(MachMsgOolDescriptorRaw);
                } else if (cur_off + sizeof(MachMsgPortDescriptorRaw) <= send_size &&
                           raw_ptr[cur_off + 11] == MACH_MSG_PORT_DESCRIPTOR) {
                    MachMsgPortDescriptorRaw pdesc{};
                    std::memcpy(&pdesc, raw_ptr + cur_off, sizeof(pdesc));
                    auto p_obj = space->lookup_port(pdesc.name);
                    if (p_obj) {
                        InFlightPortDescriptor ifpd;
                        ifpd.object = p_obj;
                        ifpd.disposition = pdesc.disposition;
                        if (pdesc.disposition == MACH_MSG_TYPE_MOVE_RECEIVE) {
                            space->deallocate_port(pdesc.name);
                        } else if (pdesc.disposition == MACH_MSG_TYPE_MOVE_SEND) {
                            space->mod_refs(pdesc.name, kMachPortRightSend, -1);
                        } else if (pdesc.disposition == MACH_MSG_TYPE_MOVE_SEND_ONCE) {
                            space->consume_send_once(pdesc.name);
                        }
                        msg.port_descriptors.push_back(std::move(ifpd));
                    }
                    cur_off += sizeof(MachMsgPortDescriptorRaw);
                } else {
                    break;
                }
            }

            if (cur_off < send_size) {
                const std::size_t rem_size = send_size - cur_off;
                msg.payload.resize(rem_size);
                std::memcpy(msg.payload.data(), raw_ptr + cur_off, rem_size);
            }
        } else {
            const std::size_t payload_size = send_size - sizeof(MachMsgHeaderRaw);
            if (payload_size > 0) {
                msg.payload.resize(payload_size);
                std::memcpy(msg.payload.data(), raw_ptr + sizeof(MachMsgHeaderRaw), payload_size);
            }
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

        std::shared_ptr<MachPortObject> rcv_obj;
        mach_port_t actual_received_port = rcv_name;

        if (space->is_port_set(rcv_name)) {
            auto members = space->get_set_members(rcv_name);
            for (mach_port_t m_name : members) {
                auto m_obj = space->lookup_port(m_name);
                if (m_obj && m_obj->has_messages()) {
                    rcv_obj = m_obj;
                    actual_received_port = m_name;
                    break;
                }
            }
            if (!rcv_obj) {
                return kMachRcvTimedOut;
            }
        } else {
            rcv_obj = space->lookup_port(rcv_name);
            if (!rcv_obj) {
                return kMachRcvInvalidName;
            }
        }

        auto dequeued = rcv_obj->dequeue_message();
        if (!dequeued.has_value()) {
            return kMachRcvTimedOut;
        }

        std::size_t rcv_off = 0;
        if (!vaddr_to_offset(target_rcv_vaddr, sizeof(MachMsgHeaderRaw), memory, rcv_off)) {
            return kMachRcvHeaderError;
        }

        auto hdr = dequeued->header;
        hdr.msgh_local_port = actual_received_port;

        if (dequeued->reply_port_object) {
            mach_port_t receiver_remote_port = space->allocate_port(dequeued->reply_port_object, kMachPortRightSend);
            hdr.msgh_remote_port = receiver_remote_port;
        }

        if (dequeued->is_complex) {
            std::size_t cur_write = rcv_off + sizeof(MachMsgHeaderRaw);
            MachMsgBodyRaw body{};
            body.msgh_descriptor_count = static_cast<std::uint32_t>(
                dequeued->port_descriptors.size() + dequeued->ool_descriptors.size());

            std::memcpy(memory.raw_data() + cur_write, &body, sizeof(body));
            cur_write += sizeof(body);

            for (const auto& pdesc : dequeued->port_descriptors) {
                mach_port_t new_name = kMachPortNull;
                if (pdesc.disposition == MACH_MSG_TYPE_MOVE_RECEIVE) {
                    pdesc.object->set_receiver_task(task_id);
                    new_name = space->allocate_port(pdesc.object, kMachPortRightReceive);
                } else if (pdesc.disposition == MACH_MSG_TYPE_MOVE_SEND_ONCE ||
                           pdesc.disposition == MACH_MSG_TYPE_MAKE_SEND_ONCE) {
                    new_name = space->allocate_port(pdesc.object, kMachPortRightSendOnce);
                } else {
                    new_name = space->allocate_port(pdesc.object, kMachPortRightSend);
                }

                MachMsgPortDescriptorRaw out_pdesc{};
                out_pdesc.name = new_name;
                out_pdesc.disposition = static_cast<std::uint8_t>(pdesc.disposition);
                out_pdesc.type = MACH_MSG_PORT_DESCRIPTOR;
                std::memcpy(memory.raw_data() + cur_write, &out_pdesc, sizeof(out_pdesc));
                cur_write += sizeof(out_pdesc);
            }

            std::uint64_t ool_dest_va = 0x60000;
            for (const auto& odesc : dequeued->ool_descriptors) {
                ool_dest_va = (ool_dest_va + 0x3FFF) & ~0x3FFFULL;
                std::size_t dest_off = 0;
                if (vaddr_to_offset(ool_dest_va, odesc.size, memory, dest_off)) {
                    std::memcpy(memory.raw_data() + dest_off, odesc.data.data(), odesc.size);
                }

                MachMsgOolDescriptorRaw out_odesc{};
                out_odesc.address = ool_dest_va;
                out_odesc.size = odesc.size;
                out_odesc.deallocate = odesc.deallocate;
                out_odesc.copy = odesc.copy;
                out_odesc.type = MACH_MSG_OOL_DESCRIPTOR;

                std::memcpy(memory.raw_data() + cur_write, &out_odesc, sizeof(out_odesc));
                cur_write += sizeof(out_odesc);
                ool_dest_va += odesc.size;
            }

            if (!dequeued->payload.empty()) {
                std::memcpy(memory.raw_data() + cur_write, dequeued->payload.data(), dequeued->payload.size());
                cur_write += dequeued->payload.size();
            }

            const std::size_t total_msg_size = cur_write - rcv_off;
            if (target_rcv_size < total_msg_size) {
                return kMachRcvTooLarge;
            }
            hdr.msgh_size = static_cast<std::uint32_t>(total_msg_size);
            std::memcpy(memory.raw_data() + rcv_off, &hdr, sizeof(hdr));
        } else {
            const std::size_t total_msg_size = sizeof(MachMsgHeaderRaw) + dequeued->payload.size();
            if (target_rcv_size < total_msg_size) {
                return kMachRcvTooLarge;
            }
            hdr.msgh_size = static_cast<std::uint32_t>(total_msg_size);
            std::memcpy(memory.raw_data() + rcv_off, &hdr, sizeof(hdr));
            if (!dequeued->payload.empty()) {
                std::memcpy(memory.raw_data() + rcv_off + sizeof(hdr),
                            dequeued->payload.data(),
                            dequeued->payload.size());
            }
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
