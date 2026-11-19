#pragma once

#include "kernel/process_identity.h"
#include "security/capability_manager.h"
#include "runtime/guest_ipc.h"

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <cstdint>

namespace winios::kernel {

enum class IpcStatus : uint32_t {
    Success = 0,
    SenderInvalid = 1,
    ReceiverInvalid = 2,
    StaleProcessHandle = 3,
    PermissionDenied = 4,
    QueueFull = 5,
    PayloadTooLarge = 6,
    PortNotFound = 7
};

inline const char* ipc_status_to_string(IpcStatus s) noexcept {
    switch (s) {
    case IpcStatus::Success: return "Success";
    case IpcStatus::SenderInvalid: return "SenderInvalid";
    case IpcStatus::ReceiverInvalid: return "ReceiverInvalid";
    case IpcStatus::StaleProcessHandle: return "StaleProcessHandle";
    case IpcStatus::PermissionDenied: return "PermissionDenied";
    case IpcStatus::QueueFull: return "QueueFull";
    case IpcStatus::PayloadTooLarge: return "PayloadTooLarge";
    case IpcStatus::PortNotFound: return "PortNotFound";
    default: return "Unknown";
    }
}

struct IpcCapabilityToken {
    uint64_t        token_id{0};
    ProcessHandle   sender;
    ProcessHandle   receiver;
    uint32_t        allowed_rights{0x1}; // 0x1: Send, 0x2: Query
    bool            is_revoked{false};
};

class IpcCapabilityBroker {
public:
    static constexpr size_t kMaxPortCapacity = 32;
    static constexpr size_t kMaxMessageBytes = 4096;

    IpcCapabilityBroker(std::shared_ptr<ProcessIdentityManager> identity_mgr,
                        std::shared_ptr<security::CapabilityManager> cap_mgr)
        : identity_mgr_(std::move(identity_mgr)),
          cap_mgr_(std::move(cap_mgr)) {}

    ~IpcCapabilityBroker() = default;

    // Issue communication capability token from sender to receiver
    IpcCapabilityToken grant_communication_right(ProcessHandle sender, ProcessHandle receiver, uint32_t rights = 0x1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validate_handle_locked(sender) || !validate_handle_locked(receiver)) {
            return IpcCapabilityToken{};
        }

        IpcCapabilityToken token{};
        token.token_id = ++token_counter_;
        token.sender = sender;
        token.receiver = receiver;
        token.allowed_rights = rights;
        token.is_revoked = false;

        tokens_[token.token_id] = token;
        return token;
    }

    void revoke_token(uint64_t token_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tokens_.find(token_id);
        if (it != tokens_.end()) {
            it->second.is_revoked = true;
        }
    }

    // Send message using capability token with strict stale generation and bounds checking
    IpcStatus send_message(uint64_t token_id,
                           ProcessHandle current_sender,
                           const void* payload,
                           size_t payload_size,
                           uint32_t message_id = 0) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. Verify token
        auto it = tokens_.find(token_id);
        if (it == tokens_.end() || it->second.is_revoked) {
            return IpcStatus::PermissionDenied;
        }

        const auto& token = it->second;

        // 2. Verify sender handle
        if (current_sender.pid != token.sender.pid || current_sender.generation != token.sender.generation) {
            return IpcStatus::SenderInvalid;
        }
        if (!validate_handle_locked(current_sender)) {
            return IpcStatus::SenderInvalid;
        }

        // 3. Verify receiver handle (check for stale generation!)
        if (!validate_handle_locked(token.receiver)) {
            // Target process died or was reused with a different generation
            return IpcStatus::StaleProcessHandle;
        }

        // 4. Verify payload size
        if (payload_size > kMaxMessageBytes) {
            return IpcStatus::PayloadTooLarge;
        }

        // 5. Check queue depth for receiver
        auto& queue = message_queues_[token.receiver.pid];
        if (queue.size() >= kMaxPortCapacity) {
            return IpcStatus::QueueFull;
        }

        // 6. Enqueue message
        runtime::IpcMessage msg{};
        msg.sender_pid = current_sender.pid;
        msg.target_port = token.receiver.pid;
        msg.message_id = message_id;
        msg.data_size = static_cast<uint32_t>(payload_size);
        if (payload && payload_size > 0) {
            const auto* bytes = static_cast<const uint8_t*>(payload);
            msg.data.assign(bytes, bytes + payload_size);
        }

        queue.push_back(msg);
        return IpcStatus::Success;
    }

    // Receive message
    IpcStatus receive_message(ProcessHandle current_receiver, runtime::IpcMessage& out_msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validate_handle_locked(current_receiver)) {
            return IpcStatus::ReceiverInvalid;
        }

        auto it = message_queues_.find(current_receiver.pid);
        if (it == message_queues_.end() || it->second.empty()) {
            return IpcStatus::PortNotFound;
        }

        out_msg = it->second.front();
        it->second.erase(it->second.begin());
        return IpcStatus::Success;
    }

    // Clean up queues and tokens for a terminated process
    void cleanup_process(uint32_t pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        message_queues_.erase(pid);
        for (auto& [_, token] : tokens_) {
            if (token.sender.pid == pid || token.receiver.pid == pid) {
                token.is_revoked = true;
            }
        }
    }

private:
    bool validate_handle_locked(ProcessHandle handle) const {
        if (!identity_mgr_) return false;
        return identity_mgr_->is_valid_handle(handle);
    }

    std::shared_ptr<ProcessIdentityManager>         identity_mgr_;
    std::shared_ptr<security::CapabilityManager>    cap_mgr_;

    mutable std::mutex                              mutex_;
    uint64_t                                        token_counter_{0};
    std::map<uint64_t, IpcCapabilityToken>          tokens_;
    std::map<uint32_t, std::vector<runtime::IpcMessage>> message_queues_;
};

} // namespace winios::kernel
