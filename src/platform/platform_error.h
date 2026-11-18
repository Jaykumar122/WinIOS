#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <optional>

namespace winios::platform {

enum class PlatformErrorCategory : uint32_t {
    Success            = 0,
    InvalidArgument    = 1,
    PermissionDenied   = 2,
    NotFound           = 3,
    ResourceExhausted  = 4,
    Timeout            = 5,
    Cancelled          = 6,
    IoError            = 7,
    ServiceUnavailable = 8,
    Conflict           = 9,
    CorruptState       = 10,
    InternalError      = 11
};

enum class PlatformDomain : uint16_t {
    Core        = 0,
    Settings    = 1,
    TaskManager = 2,
    Window      = 3,
    Document    = 4,
    Security    = 5,
    Diagnostics = 6,
    Vfs         = 7,
    Syscall     = 8,
    Service     = 9
};

class PlatformError {
public:
    constexpr PlatformError() noexcept
        : category_(PlatformErrorCategory::Success), domain_(PlatformDomain::Core), code_(0) {}

    PlatformError(PlatformErrorCategory category,
                  PlatformDomain domain,
                  uint16_t code,
                  std::string detail)
        : category_(category), domain_(domain), code_(code), detail_(std::move(detail)) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept {
        return category_ == PlatformErrorCategory::Success;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return !is_ok();
    }

    [[nodiscard]] constexpr PlatformErrorCategory category() const noexcept { return category_; }
    [[nodiscard]] constexpr PlatformDomain domain() const noexcept { return domain_; }
    [[nodiscard]] constexpr uint16_t code() const noexcept { return code_; }
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }

    [[nodiscard]] constexpr int32_t to_errno() const noexcept {
        switch (category_) {
            case PlatformErrorCategory::Success:            return 0;
            case PlatformErrorCategory::InvalidArgument:    return 22; // EINVAL
            case PlatformErrorCategory::PermissionDenied:   return 1;  // EPERM
            case PlatformErrorCategory::NotFound:           return 2;  // ENOENT
            case PlatformErrorCategory::ResourceExhausted:  return 12; // ENOMEM
            case PlatformErrorCategory::Timeout:            return 60; // ETIMEDOUT
            case PlatformErrorCategory::Cancelled:          return 89; // ECANCELED
            case PlatformErrorCategory::IoError:            return 5;  // EIO
            case PlatformErrorCategory::ServiceUnavailable: return 19; // ENODEV
            case PlatformErrorCategory::Conflict:           return 17; // EEXIST
            case PlatformErrorCategory::CorruptState:       return 14; // EFAULT
            case PlatformErrorCategory::InternalError:      return 5;  // EIO
            default:                                        return 22; // EINVAL
        }
    }

    [[nodiscard]] constexpr int32_t to_guest_status() const noexcept {
        switch (category_) {
            case PlatformErrorCategory::Success:            return 0;  // WINIOS_SUCCESS
            case PlatformErrorCategory::InvalidArgument:    return -1; // WINIOS_ERR_INVALID_PARAM
            case PlatformErrorCategory::ResourceExhausted:  return -2; // WINIOS_ERR_NO_MEMORY
            case PlatformErrorCategory::NotFound:           return -3; // WINIOS_ERR_NOT_FOUND
            case PlatformErrorCategory::PermissionDenied:   return -4; // WINIOS_ERR_ACCESS_DENIED
            case PlatformErrorCategory::IoError:            return -5; // WINIOS_ERR_IO
            case PlatformErrorCategory::Conflict:           return -6; // WINIOS_ERR_BUSY
            case PlatformErrorCategory::Timeout:            return -7; // WINIOS_ERR_TIMEOUT
            case PlatformErrorCategory::ServiceUnavailable: return -8; // WINIOS_ERR_UNSUPPORTED
            case PlatformErrorCategory::Cancelled:          return -6; // WINIOS_ERR_BUSY
            case PlatformErrorCategory::CorruptState:       return -5; // WINIOS_ERR_IO
            default:                                        return -1;
        }
    }

    [[nodiscard]] std::string to_string() const {
        if (is_ok()) return "OK";
        std::string s = "PlatformError[";
        s += category_name();
        s += ", domain=";
        s += domain_name();
        s += ", code=";
        s += std::to_string(code_);
        if (!detail_.empty()) {
            s += ", detail=\"";
            s += sanitize_detail(detail_);
            s += "\"";
        }
        s += "]";
        return s;
    }

    // Static factories
    static PlatformError ok() noexcept { return PlatformError(); }

    static PlatformError invalid_argument(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::InvalidArgument, domain, code, std::move(detail));
    }

    static PlatformError permission_denied(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::PermissionDenied, domain, code, std::move(detail));
    }

    static PlatformError not_found(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::NotFound, domain, code, std::move(detail));
    }

    static PlatformError resource_exhausted(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::ResourceExhausted, domain, code, std::move(detail));
    }

    static PlatformError timeout(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::Timeout, domain, code, std::move(detail));
    }

    static PlatformError cancelled(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::Cancelled, domain, code, std::move(detail));
    }

    static PlatformError io_error(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::IoError, domain, code, std::move(detail));
    }

    static PlatformError service_unavailable(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::ServiceUnavailable, domain, code, std::move(detail));
    }

    static PlatformError conflict(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::Conflict, domain, code, std::move(detail));
    }

    static PlatformError corrupt_state(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::CorruptState, domain, code, std::move(detail));
    }

    static PlatformError internal_error(PlatformDomain domain, std::string detail, uint16_t code = 1) {
        return PlatformError(PlatformErrorCategory::InternalError, domain, code, std::move(detail));
    }

private:
    [[nodiscard]] const char* category_name() const noexcept {
        switch (category_) {
            case PlatformErrorCategory::Success:            return "Success";
            case PlatformErrorCategory::InvalidArgument:    return "InvalidArgument";
            case PlatformErrorCategory::PermissionDenied:   return "PermissionDenied";
            case PlatformErrorCategory::NotFound:           return "NotFound";
            case PlatformErrorCategory::ResourceExhausted:  return "ResourceExhausted";
            case PlatformErrorCategory::Timeout:            return "Timeout";
            case PlatformErrorCategory::Cancelled:          return "Cancelled";
            case PlatformErrorCategory::IoError:            return "IoError";
            case PlatformErrorCategory::ServiceUnavailable: return "ServiceUnavailable";
            case PlatformErrorCategory::Conflict:           return "Conflict";
            case PlatformErrorCategory::CorruptState:       return "CorruptState";
            case PlatformErrorCategory::InternalError:      return "InternalError";
            default:                                        return "Unknown";
        }
    }

    [[nodiscard]] const char* domain_name() const noexcept {
        switch (domain_) {
            case PlatformDomain::Core:        return "Core";
            case PlatformDomain::Settings:    return "Settings";
            case PlatformDomain::TaskManager: return "TaskManager";
            case PlatformDomain::Window:      return "Window";
            case PlatformDomain::Document:    return "Document";
            case PlatformDomain::Security:    return "Security";
            case PlatformDomain::Diagnostics: return "Diagnostics";
            case PlatformDomain::Vfs:         return "Vfs";
            case PlatformDomain::Syscall:     return "Syscall";
            case PlatformDomain::Service:     return "Service";
            default:                          return "Unknown";
        }
    }

    // Strips raw host file system drive paths and sensitive patterns
    [[nodiscard]] static std::string sanitize_detail(std::string_view raw) {
        std::string out;
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            // Mask Windows drive letters e.g. "C:\" or "D:\"
            if (i + 2 < raw.size() &&
                ((raw[i] >= 'a' && raw[i] <= 'z') || (raw[i] >= 'A' && raw[i] <= 'Z')) &&
                raw[i + 1] == ':' && (raw[i + 2] == '\\' || raw[i + 2] == '/')) {
                out += "[host_path]";
                i += 2;
                continue;
            }
            out.push_back(raw[i]);
        }
        return out;
    }

    PlatformErrorCategory category_{PlatformErrorCategory::Success};
    PlatformDomain        domain_{PlatformDomain::Core};
    uint16_t              code_{0};
    std::string           detail_{};
};

template <typename T>
class PlatformResult {
public:
    PlatformResult(const T& val) : data_(val) {}
    PlatformResult(T&& val) : data_(std::move(val)) {}
    PlatformResult(PlatformError err) : data_(std::move(err)) {}

    [[nodiscard]] bool is_ok() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return is_ok();
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(data_);
    }

    [[nodiscard]] T& value() {
        return std::get<T>(data_);
    }

    [[nodiscard]] const PlatformError& error() const {
        if (is_ok()) {
            static const PlatformError kOk{};
            return kOk;
        }
        return std::get<PlatformError>(data_);
    }

private:
    std::variant<T, PlatformError> data_;
};

} // namespace winios::platform
