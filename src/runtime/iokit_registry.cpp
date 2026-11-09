#include "runtime/iokit_registry.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::runtime {

namespace {

std::vector<std::string> extract_compatible_tokens_local(const std::vector<std::uint8_t>& data) {
    std::vector<std::string> tokens;
    if (data.empty()) return tokens;

    const auto* p = reinterpret_cast<const char*>(data.data());
    std::size_t offset = 0;

    while (offset < data.size()) {
        std::size_t len = 0;
        while (offset + len < data.size() && p[offset + len] != '\0') {
            len++;
        }
        if (len > 0) {
            tokens.emplace_back(p + offset, len);
        }
        offset += len + 1;
    }

    return tokens;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// IORegistryEntry Implementation
// -----------------------------------------------------------------------------
IORegistryEntry::IORegistryEntry(std::string name)
    : name_(std::move(name)) {}

void IORegistryEntry::set_property(std::string key, std::vector<std::uint8_t> value) {
    properties_[std::move(key)] = std::move(value);
}

void IORegistryEntry::set_property_string(std::string key, std::string value) {
    std::vector<std::uint8_t> data(value.begin(), value.end());
    data.push_back(0); // Null-terminate
    set_property(std::move(key), std::move(data));
}

void IORegistryEntry::set_property_u32(std::string key, std::uint32_t value) {
    std::vector<std::uint8_t> data(sizeof(value));
    std::memcpy(data.data(), &value, sizeof(value));
    set_property(std::move(key), std::move(data));
}

void IORegistryEntry::set_property_u64(std::string key, std::uint64_t value) {
    std::vector<std::uint8_t> data(sizeof(value));
    std::memcpy(data.data(), &value, sizeof(value));
    set_property(std::move(key), std::move(data));
}

bool IORegistryEntry::has_property(std::string_view key) const noexcept {
    return properties_.find(std::string(key)) != properties_.end();
}

const std::vector<std::uint8_t>* IORegistryEntry::get_property(std::string_view key) const noexcept {
    auto it = properties_.find(std::string(key));
    if (it != properties_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string IORegistryEntry::get_property_string(std::string_view key, const std::string& default_val) const {
    const auto* p = get_property(key);
    if (!p || p->empty()) return default_val;
    return std::string(reinterpret_cast<const char*>(p->data()));
}

std::uint32_t IORegistryEntry::get_property_u32(std::string_view key, std::uint32_t default_val) const noexcept {
    const auto* p = get_property(key);
    if (!p || p->size() < sizeof(std::uint32_t)) return default_val;
    std::uint32_t val = 0;
    std::memcpy(&val, p->data(), sizeof(val));
    return val;
}

std::uint64_t IORegistryEntry::get_property_u64(std::string_view key, std::uint64_t default_val) const noexcept {
    const auto* p = get_property(key);
    if (!p || p->size() < sizeof(std::uint64_t)) return default_val;
    std::uint64_t val = 0;
    std::memcpy(&val, p->data(), sizeof(val));
    return val;
}

void IORegistryEntry::attach_to_plane(std::string_view plane, IORegistryEntry* parent) {
    const std::string pl(plane);
    parents_[pl] = parent;
    if (parent) {
        auto& sibs = parent->children_[pl];
        if (std::find(sibs.begin(), sibs.end(), this) == sibs.end()) {
            sibs.push_back(this);
        }
    }
}

void IORegistryEntry::detach_from_plane(std::string_view plane) {
    const std::string pl(plane);
    auto it = parents_.find(pl);
    if (it != parents_.end()) {
        if (it->second) {
            auto& sibs = it->second->children_[pl];
            sibs.erase(std::remove(sibs.begin(), sibs.end(), this), sibs.end());
        }
        parents_.erase(it);
    }
    children_.erase(pl);
}

IORegistryEntry* IORegistryEntry::get_parent(std::string_view plane) const noexcept {
    auto it = parents_.find(std::string(plane));
    if (it != parents_.end()) {
        return it->second;
    }
    return nullptr;
}

static const std::vector<IORegistryEntry*> kEmptyChildren;

const std::vector<IORegistryEntry*>& IORegistryEntry::get_children(std::string_view plane) const noexcept {
    auto it = children_.find(std::string(plane));
    if (it != children_.end()) {
        return it->second;
    }
    return kEmptyChildren;
}

std::string IORegistryEntry::get_path(std::string_view plane) const {
    const std::string pl(plane);
    auto it = parents_.find(pl);
    if (it == parents_.end() || it->second == nullptr) {
        return name_.empty() ? "/" : "/" + name_;
    }
    std::string parent_path = it->second->get_path(plane);
    if (parent_path == "/") {
        return "/" + name_;
    }
    return parent_path + "/" + name_;
}

bool IORegistryEntry::in_plane(std::string_view plane) const noexcept {
    return parents_.find(std::string(plane)) != parents_.end() ||
           children_.find(std::string(plane)) != children_.end();
}

// -----------------------------------------------------------------------------
// IOService Implementation
// -----------------------------------------------------------------------------
IOService::IOService(std::string name)
    : IORegistryEntry(std::move(name)) {}

bool IOService::init() {
    state_ = IOServiceState::Registered;
    return true;
}

std::int32_t IOService::probe(IOService* /*provider*/, std::int32_t* score) {
    state_ = IOServiceState::Probed;
    if (score) *score = probe_score_;
    return probe_score_;
}

bool IOService::start(IOService* /*provider*/) {
    state_ = IOServiceState::Active;
    return true;
}

void IOService::stop(IOService* /*provider*/) {
    state_ = IOServiceState::Terminated;
}

// -----------------------------------------------------------------------------
// IOPlatformDevice Implementation
// -----------------------------------------------------------------------------
IOPlatformDevice::IOPlatformDevice(std::string name)
    : IOService(std::move(name)) {}

// -----------------------------------------------------------------------------
// IOPlatformExpertDevice Implementation
// -----------------------------------------------------------------------------
IOPlatformExpertDevice::IOPlatformExpertDevice(std::string name)
    : IOPlatformDevice(std::move(name)) {}

// -----------------------------------------------------------------------------
// IORegistryManager Implementation
// -----------------------------------------------------------------------------
IORegistryManager::IORegistryManager() {
    reset();
}

void IORegistryManager::reset() {
    entries_.clear();

    service_root_ = std::make_unique<IORegistryEntry>("Root");
    service_root_->attach_to_plane(kIOServicePlane, nullptr);

    dt_root_ = std::make_unique<IORegistryEntry>("Root");
    dt_root_->attach_to_plane(kIODTPlane, nullptr);

    power_root_ = std::make_unique<IORegistryEntry>("IOPowerRoot");
    power_root_->attach_to_plane(kIOPowerPlane, nullptr);
}

IORegistryEntry* IORegistryManager::get_plane_root(std::string_view plane) const noexcept {
    if (plane == kIOServicePlane) return service_root_.get();
    if (plane == kIODTPlane)      return dt_root_.get();
    if (plane == kIOPowerPlane)   return power_root_.get();
    return nullptr;
}

IORegistryEntry* IORegistryManager::create_entry(std::string name) {
    auto entry = std::make_unique<IORegistryEntry>(std::move(name));
    auto* ptr = entry.get();
    entries_.push_back(std::move(entry));
    return ptr;
}

IOPlatformDevice* IORegistryManager::create_platform_device(std::string name) {
    auto dev = std::make_unique<IOPlatformDevice>(std::move(name));
    auto* ptr = dev.get();
    entries_.push_back(std::move(dev));
    return ptr;
}

IORegistryEntry* IORegistryManager::lookup_by_path(std::string_view plane, std::string_view path) const {
    auto* root = get_plane_root(plane);
    if (!root) return nullptr;

    if (path.empty() || path == "/" || path == "/Root" || path == "Root") {
        return root;
    }

    std::string p(path);
    if (p.front() == '/') p.erase(0, 1);

    std::stringstream ss(p);
    std::string segment;
    IORegistryEntry* current = root;

    while (std::getline(ss, segment, '/')) {
        if (segment.empty()) continue;
        const auto& children = current->get_children(plane);
        auto it = std::find_if(children.begin(), children.end(), [&](IORegistryEntry* e) {
            return e && e->name() == segment;
        });
        if (it == children.end()) {
            return nullptr;
        }
        current = *it;
    }

    return current;
}

std::size_t IORegistryManager::count_entries_in_plane(std::string_view plane) const noexcept {
    auto* root = get_plane_root(plane);
    if (!root) return 0;

    std::size_t count = 0;
    std::vector<IORegistryEntry*> q = {root};
    while (!q.empty()) {
        auto* curr = q.back();
        q.pop_back();
        count++;
        for (auto* child : curr->get_children(plane)) {
            if (child) q.push_back(child);
        }
    }
    return count;
}

void IORegistryManager::traverse_dt_node(const device::AppleDeviceTreeNode& dt_node,
                                        IORegistryEntry* dt_parent,
                                        IORegistryEntry* service_parent) {
    auto* dev = create_platform_device(dt_node.name().empty() ? "device-tree" : dt_node.name());

    // Copy properties
    for (const auto& prop : dt_node.properties()) {
        dev->set_property(prop.name, prop.data);

        if (prop.name == "compatible") {
            dev->set_compatible_tokens(extract_compatible_tokens_local(prop.data));
        } else if (prop.name == "reg" && prop.data.size() >= 16) {
            std::uint64_t reg_vals[2] = {0, 0};
            std::memcpy(reg_vals, prop.data.data(), sizeof(reg_vals));
            dev->set_mmio(reg_vals[0], reg_vals[1]);
        } else if (prop.name == "interrupts" && prop.data.size() >= 4) {
            std::uint32_t irq = 0;
            std::memcpy(&irq, prop.data.data(), sizeof(irq));
            dev->set_irq_line(irq);
        }
    }

    // Attach to gIODTPlane
    dev->attach_to_plane(kIODTPlane, dt_parent);

    // Attach to gIOServicePlane
    dev->attach_to_plane(kIOServicePlane, service_parent);

    // Power plane linkage: devices under arm-io link under power_root_ or PMGR
    if (dev->name() == "pmgr") {
        dev->attach_to_plane(kIOPowerPlane, power_root_.get());
    } else if (service_parent && service_parent->in_plane(kIOPowerPlane)) {
        dev->attach_to_plane(kIOPowerPlane, service_parent);
    }

    // Recursively process children
    for (const auto& child : dt_node.children()) {
        if (child) {
            traverse_dt_node(*child, dev, dev);
        }
    }
}

bool IORegistryManager::build_from_devicetree(const device::AppleDeviceTreeNode& dt_root, std::string* /*out_error*/) {
    reset();

    // The root of the device tree mirrors under Root
    traverse_dt_node(dt_root, dt_root_.get(), service_root_.get());

    return true;
}

} // namespace winios::runtime
