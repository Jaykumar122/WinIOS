#include "kernel/bootstrap_registry.h"

#include <algorithm>

namespace winios::kernel {

int BootstrapServiceRegistry::check_in(
    const std::string& name,
    std::uint32_t port_id,
    pid_t caller_pid,
    bool require_privilege) {
    if (name.empty() || port_id == 0) {
        return kBootstrapUnknownService;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (services_.size() >= kMaxServices) {
        return kBootstrapNotPrivileged;
    }

    auto it = services_.find(name);
    if (it != services_.end()) {
        if (it->second.state == ServiceState::Active) {
            return kBootstrapNameInUse;
        }
        // If dead or unassigned, allow re-registration by authorized caller
        if (it->second.system_privileged && caller_pid != 1) {
            return kBootstrapNotPrivileged;
        }
        it->second.port_id = port_id;
        it->second.owner_pid = caller_pid;
        it->second.state = ServiceState::Active;
        return kBootstrapSuccess;
    }

    if (require_privilege && caller_pid != 1) {
        return kBootstrapNotPrivileged;
    }

    ServiceRecord rec{};
    rec.name = name;
    rec.port_id = port_id;
    rec.owner_pid = caller_pid;
    rec.state = ServiceState::Active;
    rec.system_privileged = require_privilege;
    rec.lookup_count = 0;

    services_[name] = std::move(rec);
    return kBootstrapSuccess;
}

int BootstrapServiceRegistry::look_up(
    const std::string& name,
    std::uint32_t& out_port_id,
    pid_t /* caller_pid */) {
    out_port_id = 0;
    if (name.empty()) {
        return kBootstrapUnknownService;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it == services_.end()) {
        return kBootstrapUnknownService;
    }

    if (it->second.state == ServiceState::Dead) {
        return kBootstrapUnknownService;
    }

    it->second.lookup_count++;
    out_port_id = it->second.port_id;
    return kBootstrapSuccess;
}

int BootstrapServiceRegistry::unregister(const std::string& name, pid_t caller_pid) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it == services_.end()) {
        return kBootstrapUnknownService;
    }

    // Only owner or init (PID 1) can unregister
    if (it->second.owner_pid != caller_pid && caller_pid != 1) {
        return kBootstrapNotPrivileged;
    }

    services_.erase(it);
    return kBootstrapSuccess;
}

void BootstrapServiceRegistry::handle_process_termination(pid_t terminated_pid) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [name, record] : services_) {
        if (record.owner_pid == terminated_pid && record.state == ServiceState::Active) {
            record.state = ServiceState::Dead;
            dead_names_.push_back(name);
        }
    }
}

bool BootstrapServiceRegistry::get_service_info(const std::string& name, ServiceRecord& out_record) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = services_.find(name);
    if (it == services_.end()) {
        return false;
    }

    out_record = it->second;
    return true;
}

std::size_t BootstrapServiceRegistry::service_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.size();
}

bool BootstrapServiceRegistry::is_service_registered(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.find(name) != services_.end();
}

bool BootstrapServiceRegistry::is_service_active(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = services_.find(name);
    return (it != services_.end() && it->second.state == ServiceState::Active);
}

void BootstrapServiceRegistry::serialize(std::vector<std::uint8_t>& dst) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Serialize services count
    const std::uint32_t count = static_cast<std::uint32_t>(services_.size());
    const auto* p_count = reinterpret_cast<const std::uint8_t*>(&count);
    dst.insert(dst.end(), p_count, p_count + sizeof(count));

    for (const auto& [name, rec] : services_) {
        // Name
        const std::uint32_t name_len = static_cast<std::uint32_t>(rec.name.size());
        const auto* p_nl = reinterpret_cast<const std::uint8_t*>(&name_len);
        dst.insert(dst.end(), p_nl, p_nl + sizeof(name_len));
        dst.insert(dst.end(), rec.name.begin(), rec.name.end());

        // Fields
        const auto* p_pid = reinterpret_cast<const std::uint8_t*>(&rec.port_id);
        dst.insert(dst.end(), p_pid, p_pid + sizeof(rec.port_id));

        const auto* p_opid = reinterpret_cast<const std::uint8_t*>(&rec.owner_pid);
        dst.insert(dst.end(), p_opid, p_opid + sizeof(rec.owner_pid));

        const std::uint8_t state_val = static_cast<std::uint8_t>(rec.state);
        dst.push_back(state_val);

        const std::uint8_t priv_val = rec.system_privileged ? 1 : 0;
        dst.push_back(priv_val);

        const auto* p_lc = reinterpret_cast<const std::uint8_t*>(&rec.lookup_count);
        dst.insert(dst.end(), p_lc, p_lc + sizeof(rec.lookup_count));
    }

    // 2. Serialize dead names
    const std::uint32_t dn_count = static_cast<std::uint32_t>(dead_names_.size());
    const auto* p_dnc = reinterpret_cast<const std::uint8_t*>(&dn_count);
    dst.insert(dst.end(), p_dnc, p_dnc + sizeof(dn_count));
    for (const auto& dn : dead_names_) {
        const std::uint32_t dn_len = static_cast<std::uint32_t>(dn.size());
        const auto* p_dnl = reinterpret_cast<const std::uint8_t*>(&dn_len);
        dst.insert(dst.end(), p_dnl, p_dnl + sizeof(dn_len));
        dst.insert(dst.end(), dn.begin(), dn.end());
    }
}

bool BootstrapServiceRegistry::deserialize(const std::uint8_t*& src, std::size_t& remaining) {
    std::lock_guard<std::mutex> lock(mutex_);
    services_.clear();
    dead_names_.clear();

    if (remaining < sizeof(std::uint32_t)) return false;
    std::uint32_t count = 0;
    std::memcpy(&count, src, sizeof(count));
    src += sizeof(count);
    remaining -= sizeof(count);

    for (std::uint32_t i = 0; i < count; ++i) {
        if (remaining < sizeof(std::uint32_t)) return false;
        std::uint32_t name_len = 0;
        std::memcpy(&name_len, src, sizeof(name_len));
        src += sizeof(name_len);
        remaining -= sizeof(name_len);

        if (remaining < name_len) return false;
        std::string name(reinterpret_cast<const char*>(src), name_len);
        src += name_len;
        remaining -= name_len;

        constexpr std::size_t kFieldsSize = sizeof(std::uint32_t) + sizeof(pid_t) + 1 + 1 + sizeof(std::uint64_t);
        if (remaining < kFieldsSize) return false;

        ServiceRecord rec{};
        rec.name = name;
        std::memcpy(&rec.port_id, src, sizeof(rec.port_id));
        src += sizeof(rec.port_id);
        remaining -= sizeof(rec.port_id);

        std::memcpy(&rec.owner_pid, src, sizeof(rec.owner_pid));
        src += sizeof(rec.owner_pid);
        remaining -= sizeof(rec.owner_pid);

        rec.state = static_cast<ServiceState>(*src++);
        --remaining;

        rec.system_privileged = (*src++ != 0);
        --remaining;

        std::memcpy(&rec.lookup_count, src, sizeof(rec.lookup_count));
        src += sizeof(rec.lookup_count);
        remaining -= sizeof(rec.lookup_count);

        services_[name] = rec;
    }

    if (remaining < sizeof(std::uint32_t)) return false;
    std::uint32_t dn_count = 0;
    std::memcpy(&dn_count, src, sizeof(dn_count));
    src += sizeof(dn_count);
    remaining -= sizeof(dn_count);

    for (std::uint32_t i = 0; i < dn_count; ++i) {
        if (remaining < sizeof(std::uint32_t)) return false;
        std::uint32_t dn_len = 0;
        std::memcpy(&dn_len, src, sizeof(dn_len));
        src += sizeof(dn_len);
        remaining -= sizeof(dn_len);

        if (remaining < dn_len) return false;
        std::string dn(reinterpret_cast<const char*>(src), dn_len);
        src += dn_len;
        remaining -= dn_len;
        dead_names_.push_back(dn);
    }

    return true;
}

} // namespace winios::kernel
