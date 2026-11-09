#pragma once
#include <filesystem>
#include <span>
#include <string>
#include <cstdint>

enum class PublicationFault { none, write, flush, close, publish };
// Deterministic, thread-local test seam; never configured by CLI/environment.
class ScopedPublicationFault {
public:
    ScopedPublicationFault(PublicationFault fault, std::string extension = {});
    ~ScopedPublicationFault();
    ScopedPublicationFault(const ScopedPublicationFault&) = delete;
    ScopedPublicationFault& operator=(const ScopedPublicationFault&) = delete;
private:
    PublicationFault previous_;
    std::string previous_extension_;
};
class ArtifactPublication {
public:
    static constexpr std::size_t kMaxBytes = 65u * 1024u * 1024u;
    explicit ArtifactPublication(const std::filesystem::path& destination,
                                 const std::string& test_temporary_name = {});
    ~ArtifactPublication();
    ArtifactPublication(const ArtifactPublication&) = delete;
    ArtifactPublication& operator=(const ArtifactPublication&) = delete;
    const std::filesystem::path& temporary_path() const noexcept { return temporary_; }
    void write(std::span<const std::uint8_t> bytes);
    // Producers using temporary_path must close/check their stream before commit.
    void commit();
    // Deletes only this transaction's successfully reserved temporary file.
    bool abort() noexcept;
private:
    std::filesystem::path destination_, temporary_;
    void* handle_{};
    bool owned_{false};
    PublicationFault fault_{PublicationFault::none};
};
