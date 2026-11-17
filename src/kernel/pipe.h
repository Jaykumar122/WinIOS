#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace winios::kernel {

class Pipe {
public:
    static constexpr std::size_t kDefaultCapacity = 65536; // 64 KB POSIX pipe buffer

    explicit Pipe(std::size_t capacity = kDefaultCapacity);
    ~Pipe() = default;

    // Writes data into the bounded ring buffer.
    // Returns bytes written, or -EPIPE (32) if the read end is closed.
    std::int64_t write(const void* buf, std::size_t count, bool non_blocking = false);

    // Reads data from the bounded ring buffer.
    // Returns bytes read, 0 on EOF (write end closed and buffer empty), or -EAGAIN (35).
    std::int64_t read(void* buf, std::size_t count, bool non_blocking = false);

    // Close endpoints
    void close_read() noexcept;
    void close_write() noexcept;

    [[nodiscard]] bool is_read_closed() const noexcept;
    [[nodiscard]] bool is_write_closed() const noexcept;
    [[nodiscard]] std::size_t bytes_available() const noexcept;
    [[nodiscard]] std::size_t space_available() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    const std::size_t          capacity_;
    std::vector<std::uint8_t>  buffer_;
    std::size_t                head_{0}; // Write index
    std::size_t                tail_{0}; // Read index
    std::size_t                count_{0};
    bool                       read_closed_{false};
    bool                       write_closed_{false};
    mutable std::mutex         mutex_;
};

} // namespace winios::kernel
