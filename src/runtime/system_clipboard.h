#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "security/capability_manager.h"

namespace winios::runtime {

struct ClipboardAuditRecord {
    std::uint64_t timestamp_us{0};
    std::uint32_t caller_pid{0};
    std::string   action; // "set", "get", "clear"
    std::size_t   data_bytes{0};
    bool          allowed{true};
};

class SystemClipboard {
public:
    static constexpr std::size_t kMaxClipboardBytes = 64 * 1024; // 64 KB

    explicit SystemClipboard(std::shared_ptr<security::CapabilityManager> caps = nullptr);
    ~SystemClipboard() = default;

    SystemClipboard(const SystemClipboard&) = delete;
    SystemClipboard& operator=(const SystemClipboard&) = delete;

    void set_capability_manager(std::shared_ptr<security::CapabilityManager> caps) {
        caps_ = std::move(caps);
    }

    // Set clipboard text (requires ClipboardWrite)
    bool set_text(std::uint32_t caller_pid, const std::string& text);

    // Get clipboard text (requires ClipboardRead)
    bool get_text(std::uint32_t caller_pid, std::string& out_text);

    // Clear clipboard (requires ClipboardWrite)
    bool clear(std::uint32_t caller_pid);

    [[nodiscard]] bool has_data() const noexcept;
    [[nodiscard]] std::size_t data_size() const noexcept;
    [[nodiscard]] const std::vector<ClipboardAuditRecord>& audit_log() const noexcept;

private:
    std::shared_ptr<security::CapabilityManager> caps_;
    std::string                                  content_;
    mutable std::mutex                           lock_;
    std::vector<ClipboardAuditRecord>            audit_log_;
};

} // namespace winios::runtime
