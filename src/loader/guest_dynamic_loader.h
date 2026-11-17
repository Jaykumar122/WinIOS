#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

namespace winios::loader {

struct LoadedDylibInfo {
    std::string name;
    std::string dylib_id;
    std::uint64_t base_va{0};
    std::uint64_t size{0};
    std::int64_t slide{0};
    std::vector<std::uint64_t> allocated_pas;
    std::unordered_map<std::string, std::uint64_t> exported_symbols;
};

class GuestDynamicLoader {
public:
    static constexpr std::uint64_t kDefaultDylibBaseVa = 0x20000000ULL; // 512 MiB

    explicit GuestDynamicLoader(
        memory::UserAddressSpace& uas,
        memory::PhysicalPageManager& ppm,
        Memory& mem);
    ~GuestDynamicLoader() = default;

    // Loads a dylib binary into guest address space at non-overlapping VA
    bool load_library(
        const std::string& name,
        const std::vector<std::uint8_t>& binary,
        LoadedDylibInfo* out_info = nullptr,
        std::string* out_err = nullptr);

    // Resolves and binds imported symbols in a client Mach-O image using loaded libraries
    bool resolve_and_bind_symbols(
        const std::vector<std::uint8_t>& binary,
        std::uint64_t image_slide = 0,
        std::string* out_err = nullptr);

    // Looks up an exported symbol address by name across all loaded libraries
    [[nodiscard]] std::uint64_t find_exported_symbol(const std::string& name) const;

    // Returns the embedded genuine ARM64 libwinios_runtime.dylib binary
    [[nodiscard]] static std::vector<std::uint8_t> get_embedded_runtime_library();

    [[nodiscard]] const std::unordered_map<std::string, LoadedDylibInfo>& loaded_libraries() const noexcept {
        return loaded_libs_;
    }

    [[nodiscard]] bool is_loaded(const std::string& name) const noexcept {
        return loaded_libs_.find(name) != loaded_libs_.end();
    }

    [[nodiscard]] std::size_t loaded_count() const noexcept {
        return loaded_libs_.size();
    }

private:
    memory::UserAddressSpace& uas_;
    memory::PhysicalPageManager& ppm_;
    Memory& mem_;
    std::uint64_t next_dylib_va_{kDefaultDylibBaseVa};
    std::unordered_map<std::string, LoadedDylibInfo> loaded_libs_{};
    std::unordered_map<std::string, std::uint64_t> global_exports_{};
};

} // namespace winios::loader
