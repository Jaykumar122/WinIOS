#include "runtime/artifact_publication.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace {
thread_local PublicationFault fault = PublicationFault::none;
thread_local std::string fault_extension;
std::atomic<std::uint64_t> sequence{0};
[[noreturn]] void failed(const char* operation) {
#ifdef _WIN32
    throw std::runtime_error(std::string("Artifact publication ") + operation + " failed (Win32 " + std::to_string(GetLastError()) + ")");
#else
    throw std::runtime_error(std::string("Artifact publication unavailable: ") + operation);
#endif
}
}
ScopedPublicationFault::ScopedPublicationFault(PublicationFault value, std::string extension)
    : previous_(fault), previous_extension_(std::move(fault_extension)) {
    fault = value; fault_extension = std::move(extension);
}
ScopedPublicationFault::~ScopedPublicationFault() { fault = previous_; fault_extension = std::move(previous_extension_); }
ArtifactPublication::ArtifactPublication(const std::filesystem::path& destination, const std::string& test_name)
    : destination_(std::filesystem::absolute(destination)) {
#ifdef _WIN32
    if (std::filesystem::exists(destination_)) throw std::invalid_argument("Artifact destination exists");
    if (!test_name.empty() && (std::filesystem::path(test_name).filename() != test_name
        || !test_name.starts_with(".winios-") || !test_name.ends_with(".tmp"))) {
        throw std::invalid_argument("Invalid temporary basename");
    }
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        const auto name = test_name.empty()
            ? ".winios-publish-" + std::to_string(GetCurrentProcessId()) + "-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "-" + std::to_string(sequence++) + ".tmp"
            : test_name;
        temporary_ = destination_.parent_path() / name;
        const HANDLE handle = CreateFileW(temporary_.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            handle_ = handle; owned_ = true;
            if (fault_extension.empty() || destination_.extension().string() == fault_extension) fault_ = fault;
            return;
        }
        if (!test_name.empty() || (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)) failed("temporary creation");
    }
    failed("temporary collision retries");
#else
    (void)test_name;
    failed("requires Windows atomic no-clobber publication");
#endif
}
ArtifactPublication::~ArtifactPublication() { (void)abort(); }
void ArtifactPublication::write(std::span<const std::uint8_t> bytes) {
#ifdef _WIN32
    if (!handle_ || !owned_ || bytes.size() > kMaxBytes) throw std::invalid_argument("Invalid publication write");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle_, &size) || size.QuadPart < 0
        || static_cast<std::uint64_t>(size.QuadPart) > kMaxBytes - bytes.size()) failed("write quota");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset,
            fault_ == PublicationFault::write ? 1u : 1024u * 1024u));
        DWORD written{};
        if (!WriteFile(handle_, bytes.data() + offset, chunk, &written, nullptr) || written == 0) failed("write");
        offset += written;
        if (fault_ == PublicationFault::write) failed("injected partial write");
    }
#else
    (void)bytes; failed("write");
#endif
}
void ArtifactPublication::commit() {
#ifdef _WIN32
    if (!owned_ || !handle_) throw std::invalid_argument("Publication is not active");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle_, &size) || size.QuadPart <= 0
        || static_cast<std::uint64_t>(size.QuadPart) > kMaxBytes) failed("size validation");
    if (fault_ == PublicationFault::write) failed("injected producer write");
    if (fault_ == PublicationFault::flush || !FlushFileBuffers(handle_)) failed("flush");
    const bool closed = CloseHandle(handle_) != 0;
    if (closed) handle_ = nullptr;
    if (!closed || fault_ == PublicationFault::close) failed("close");
    if (fault_ == PublicationFault::publish
        || !MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_WRITE_THROUGH)) failed("promotion");
    owned_ = false;
#else
    failed("commit");
#endif
}
bool ArtifactPublication::abort() noexcept {
#ifdef _WIN32
    if (handle_) {
        if (!CloseHandle(handle_)) return false;
        handle_ = nullptr;
    }
    if (owned_) {
        if (!DeleteFileW(temporary_.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
        owned_ = false;
    }
#endif
    return true;
}
