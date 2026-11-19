#pragma once

#include "kernel/process_identity.h"
#include "security/capability_manager.h"
#include "security/security_utils.h"
#include "runtime/guest_ipc.h"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
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
    PortNotFound = 7,
    ReplayDetected = 8
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
    case IpcStatus::ReplayDetected: return "ReplayDetected";
    default: return "Unknown";
    }
}

struct IpcCapabilityToken {
    uint64_t        token_id{0};          // Non-zero 64-bit ID for compatibility
    std::string     token_hex{};          // 128-bit CSPRNG hex string (32 hex characters)
    ProcessHandle   sender;               // Bound to sender PID and generation
    ProcessHandle   receiver;             // Bound to receiver PID and generation
    uint32_t        allowed_rights{0x1};  // 0x1: Send, 0x2: Query, 0x4: Admin
    bool            is_revoked{false};
    uint64_t        last_sequence_num{0}; // Anti-replay sequence tracking
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

    // Issue communication capability token with 128-bit CSPRNG hex token
    IpcCapabilityToken grant_communication_right(ProcessHandle sender, ProcessHandle receiver, uint32_t rights = 0x1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validate_handle_locked(sender) || !validate_handle_locked(receiver)) {
            return IpcCapabilityToken{};
        }

        IpcCapabilityToken token{};
        // Generate non-zero 64-bit ID
        uint64_t rand_id = security::SecureRandom::get_random_u64();
        while (rand_id == 0 || tokens_.find(rand_id) != tokens_.end()) {
            rand_id = security::SecureRandom::get_random_u64();
        }
        token.token_id = rand_id;

        // Generate 128-bit CSPRNG hex token (32 hex characters)
        token.token_hex = security::SecureRandom::get_random_hex(16);
        while (token_by_hex_.find(token.token_hex) != token_by_hex_.end()) {
            token.token_hex = security::SecureRandom::get_random_hex(16);
        }

        token.sender = sender;
        token.receiver = receiver;
        token.allowed_rights = rights;
        token.is_revoked = false;
        token.last_sequence_num = 0;

        tokens_[token.token_id] = token;
        token_by_hex_[token.token_hex] = token.token_id;
        return token;
    }

    void revoke_token(uint64_t token_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tokens_.find(token_id);
        if (it != tokens_.end()) {
            it->second.is_revoked = true;
        }
    }

    void revoke_token_by_hex(const std::string& token_hex) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it_hex = token_by_hex_.find(token_hex);
        if (it_hex != token_by_hex_.end()) {
            auto it = tokens_.find(it_hex->second);
            if (it != tokens_.end()) {
                it->second.is_revoked = true;
            }
        }
    }

    // Send message using capability token with strict stale generation, bounds checking, and anti-replay
    IpcStatus send_message(uint64_t token_id,
                           ProcessHandle current_sender,
                           const void* payload,
                           size_t payload_size,
                           uint32_t message_id = 0,
                           uint64_t sequence_num = 0) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. Verify token
        auto it = tokens_.find(token_id);
        if (it == tokens_.end() || it->second.is_revoked) {
            return IpcStatus::PermissionDenied;
        }

        auto& token = it->second;

        // Check send right (0x1)
        if ((token.allowed_rights & 0x1) == 0) {
            return IpcStatus::PermissionDenied;
        }

        // 2. Anti-replay verification (if non-zero sequence number supplied)
        if (sequence_num > 0) {
            if (sequence_num <= token.last_sequence_num) {
                return IpcStatus::ReplayDetected;
            }
            token.last_sequence_num = sequence_num;
        }

        // 3. Verify sender handle
        if (current_sender.pid != token.sender.pid || current_sender.generation != token.sender.generation) {
            return IpcStatus::SenderInvalid;
        }
        if (!validate_handle_locked(current_sender)) {
            return IpcStatus::SenderInvalid;
        }

        // 4. Verify receiver handle (check for stale generation)
        if (!validate_handle_locked(token.receiver)) {
            return IpcStatus::StaleProcessHandle;
        }

        // 5. Verify payload size
        if (payload_size > kMaxMessageBytes) {
            return IpcStatus::PayloadTooLarge;
        }

        // 6. Check queue depth for receiver
        auto& queue = message_queues_[token.receiver.pid];
        if (queue.size() >= kMaxPortCapacity) {
            return IpcStatus::QueueFull;
        }

        // 7. Enqueue message
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

    // Overload sending by 128-bit hex token
    IpcStatus send_message_by_hex(const std::string& token_hex,
                                  ProcessHandle current_sender,
                                  const void* payload,
                                  size_t payload_size,
                                  uint32_t message_id = 0,
                                  uint64_t sequence_num = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it_hex = token_by_hex_.find(token_hex);
        if (it_hex == token_by_hex_.end()) {
            return IpcStatus::PermissionDenied;
        }
        // Unlock to call send_message or delegate directly
        uint64_t tid = it_hex->second;
        // Call internal helper
        return send_message_locked(tid, current_sender, payload, payload_size, message_id, sequence_num);
    }

    // Overload validating sender process security token as well
    IpcStatus send_message_secure(const std::string& token_hex,
                                  ProcessHandle current_sender,
                                  const std::string& sender_process_token,
                                  const void* payload,
                                  size_t payload_size,
                                  uint32_t message_id = 0,
                                  uint64_t sequence_num = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (identity_mgr_ && !identity_mgr_->validate_handle_and_token(current_sender, sender_process_token)) {
            return IpcStatus::SenderInvalid;
        }
        auto it_hex = token_by_hex_.find(token_hex);
        if (it_hex == token_by_hex_.end()) {
            return IpcStatus::PermissionDenied;
        }
        return send_message_locked(it_hex->second, current_sender, payload, payload_size, message_id, sequence_num);
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

    // Clean up queues and tokens for a terminated process by PID
    void cleanup_process(uint32_t pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        message_queues_.erase(pid);
        for (auto& [_, token] : tokens_) {
            if (token.sender.pid == pid || token.receiver.pid == pid) {
                token.is_revoked = true;
            }
        }
    }

    // Clean up queues and tokens for a terminated process by ProcessHandle
    void cleanup_process(ProcessHandle handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        message_queues_.erase(handle.pid);
        for (auto& [_, token] : tokens_) {
            if ((token.sender.pid == handle.pid && token.sender.generation == handle.generation) ||
                (token.receiver.pid == handle.pid && token.receiver.generation == handle.generation)) {
                token.is_revoked = true;
            }
        }
    }

    std::optional<IpcCapabilityToken> get_token_by_id(uint64_t token_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tokens_.find(token_id);
        if (it != tokens_.end()) return it->second;
        return std::nullopt;
    }

    std::optional<IpcCapabilityToken> get_token_by_hex(const std::string& token_hex) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it_hex = token_by_hex_.find(token_hex);
        if (it_hex != token_by_hex_.end()) {
            auto it = tokens_.find(it_hex->second);
            if (it != tokens_.end()) return it->second;
        }
        return std::nullopt;
    }

    size_t queue_depth(uint32_t pid) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = message_queues_.find(pid);
        if (it != message_queues_.end()) return it->second.size();
        return 0;
    }

private:
    bool validate_handle_locked(ProcessHandle handle) const {
        if (!identity_mgr_) return false;
        return identity_mgr_->is_valid_handle(handle);
    }

    IpcStatus send_message_locked(uint64_t token_id,
                                  ProcessHandle current_sender,
                                  const void* payload,
                                  size_t payload_size,
                                  uint32_t message_id,
                                  uint64_t sequence_num) {
        auto it = tokens_.find(token_id);
        if (it == tokens_.end() || it->second.is_revoked) {
            return IpcStatus::PermissionDenied;
        }

        auto& token = it->second;
        if ((token.allowed_rights & 0x1) == 0) {
            return IpcStatus::PermissionDenied;
        }

        if (sequence_num > 0) {
            if (sequence_num <= token.last_sequence_num) {
                return IpcStatus::ReplayDetected;
            }
            token.last_sequence_num = sequence_num;
        }

        if (current_sender.pid != token.sender.pid || current_sender.generation != token.sender.generation) {
            return IpcStatus::SenderInvalid;
        }
        if (!validate_handle_locked(current_sender)) {
            return IpcStatus::SenderInvalid;
        }

        if (!validate_handle_locked(token.receiver)) {
            return IpcStatus::StaleProcessHandle;
        }

        if (payload_size > kMaxMessageBytes) {
            return IpcStatus::PayloadTooLarge;
        }

        auto& queue = message_queues_[token.receiver.pid];
        if (queue.size() >= kMaxPortCapacity) {
            return IpcStatus::QueueFull;
        }

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

    std::shared_ptr<ProcessIdentityManager>         identity_mgr_;
    std::shared_ptr<security::CapabilityManager>    cap_mgr_;

    mutable std::mutex                              mutex_;
    std::map<uint64_t, IpcCapabilityToken>          tokens_;
    std::unordered_map<std::string, uint64_t>       token_by_hex_;
    std::map<uint32_t, std::vector<runtime::IpcMessage>> message_queues_;
};

} // namespace winios::kernel
