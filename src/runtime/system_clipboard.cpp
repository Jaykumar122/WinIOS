#include "runtime/system_clipboard.h"

#include <chrono>

namespace winios::runtime {

SystemClipboard::SystemClipboard(std::shared_ptr<security::CapabilityManager> caps)
    : caps_(std::move(caps)) {}

bool SystemClipboard::set_text(std::uint32_t caller_pid, const std::string& text) {
    std::lock_guard<std::mutex> guard(lock_);
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    // Check capability
    bool allowed = true;
    if (caps_) {
        allowed = caps_->check(caller_pid, security::Capability::ClipboardWrite);
    }

    ClipboardAuditRecord rec{};
    rec.timestamp_us = now;
    rec.caller_pid = caller_pid;
    rec.action = "set";
    rec.data_bytes = text.size();
    rec.allowed = allowed;
    audit_log_.push_back(rec);

    if (!allowed) return false;
    if (text.size() > kMaxClipboardBytes) return false;

    content_ = text;
    return true;
}

bool SystemClipboard::get_text(std::uint32_t caller_pid, std::string& out_text) {
    std::lock_guard<std::mutex> guard(lock_);
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    bool allowed = true;
    if (caps_) {
        allowed = caps_->check(caller_pid, security::Capability::ClipboardRead);
    }

    ClipboardAuditRecord rec{};
    rec.timestamp_us = now;
    rec.caller_pid = caller_pid;
    rec.action = "get";
    rec.data_bytes = content_.size();
    rec.allowed = allowed;
    audit_log_.push_back(rec);

    if (!allowed) {
        out_text.clear();
        return false;
    }

    out_text = content_;
    return true;
}

bool SystemClipboard::clear(std::uint32_t caller_pid) {
    std::lock_guard<std::mutex> guard(lock_);
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    bool allowed = true;
    if (caps_) {
        allowed = caps_->check(caller_pid, security::Capability::ClipboardWrite);
    }

    ClipboardAuditRecord rec{};
    rec.timestamp_us = now;
    rec.caller_pid = caller_pid;
    rec.action = "clear";
    rec.data_bytes = 0;
    rec.allowed = allowed;
    audit_log_.push_back(rec);

    if (!allowed) return false;

    content_.clear();
    return true;
}

bool SystemClipboard::has_data() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return !content_.empty();
}

std::size_t SystemClipboard::data_size() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return content_.size();
}

const std::vector<ClipboardAuditRecord>& SystemClipboard::audit_log() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return audit_log_;
}

} // namespace winios::runtime
