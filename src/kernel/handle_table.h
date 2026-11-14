#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace winios::kernel {

enum class KernelObjectType : std::uint32_t {
    Unknown = 0,
    Thread = 1,
    IpcPort = 2,
    MemoryRegion = 3,
    Device = 4,
    SyncPrimitive = 5
};

namespace HandleRights {
    inline constexpr std::uint32_t Read = 0x01;
    inline constexpr std::uint32_t Write = 0x02;
    inline constexpr std::uint32_t Execute = 0x04;
    inline constexpr std::uint32_t Transfer = 0x08;
    inline constexpr std::uint32_t Duplicate = 0x10;
    inline constexpr std::uint32_t Destroy = 0x20;
    inline constexpr std::uint32_t All = 0x3F;
}

class KernelObject {
public:
    explicit KernelObject(KernelObjectType type, std::uint64_t id = 0);
    virtual ~KernelObject() = default;

    [[nodiscard]] KernelObjectType object_type() const noexcept { return type_; }
    [[nodiscard]] std::uint64_t object_id() const noexcept { return id_; }

    void add_ref() noexcept { ++ref_count_; }
    std::uint32_t release() noexcept {
        return --ref_count_;
    }
    [[nodiscard]] std::uint32_t ref_count() const noexcept { return ref_count_; }

private:
    KernelObjectType type_{KernelObjectType::Unknown};
    std::uint64_t id_{0};
    std::uint32_t ref_count_{1};
};

struct HandleEntry {
    std::shared_ptr<KernelObject> object{nullptr};
    std::uint32_t rights{0};
    bool valid{false};
};

class HandleTable {
public:
    explicit HandleTable(std::size_t max_handles = 1024);
    ~HandleTable() = default;

    // Allocate handle for object with specific granted rights
    std::int32_t allocate_handle(std::shared_ptr<KernelObject> object, std::uint32_t rights = HandleRights::All);

    // Retrieve object with rights validation
    [[nodiscard]] std::shared_ptr<KernelObject> get_object(std::int32_t handle, std::uint32_t required_rights = 0) const;

    // Duplicate handle with optional rights attenuation (cannot add rights)
    std::int32_t duplicate_handle(std::int32_t handle, std::uint32_t new_rights_mask = HandleRights::All);

    // Close handle, releasing reference
    bool close_handle(std::int32_t handle);

    // Query status
    [[nodiscard]] bool is_valid_handle(std::int32_t handle) const noexcept;
    [[nodiscard]] std::uint32_t get_rights(std::int32_t handle) const noexcept;
    [[nodiscard]] std::size_t active_handles_count() const noexcept { return active_count_; }
    [[nodiscard]] std::size_t max_handles() const noexcept { return max_handles_; }

private:
    std::size_t max_handles_{1024};
    std::size_t active_count_{0};
    std::vector<HandleEntry> entries_;
    std::vector<std::int32_t> free_list_;
};

} // namespace winios::kernel
