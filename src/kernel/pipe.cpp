#include "kernel/pipe.h"

#include <algorithm>
#include <cstring>

namespace winios::kernel {

Pipe::Pipe(std::size_t capacity)
    : capacity_(capacity > 0 ? capacity : kDefaultCapacity),
      buffer_(capacity_) {}

std::int64_t Pipe::write(const void* buf, std::size_t count, bool non_blocking) {
    if (!buf || count == 0) return 0;

    std::lock_guard<std::mutex> lock(mutex_);

    if (read_closed_) {
        return -32; // -EPIPE
    }

    if (count_ >= capacity_) {
        if (non_blocking) return -35; // -EAGAIN
        return 0;
    }

    const std::size_t writable = std::min(count, capacity_ - count_);
    const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(buf);

    for (std::size_t i = 0; i < writable; ++i) {
        buffer_[head_] = src[i];
        head_ = (head_ + 1) % capacity_;
    }

    count_ += writable;
    return static_cast<std::int64_t>(writable);
}

std::int64_t Pipe::read(void* buf, std::size_t count, bool non_blocking) {
    if (!buf || count == 0) return 0;

    std::lock_guard<std::mutex> lock(mutex_);

    if (count_ == 0) {
        if (write_closed_) {
            return 0; // EOF
        }
        if (non_blocking) return -35; // -EAGAIN
        return 0;
    }

    const std::size_t readable = std::min(count, count_);
    std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(buf);

    for (std::size_t i = 0; i < readable; ++i) {
        dst[i] = buffer_[tail_];
        tail_ = (tail_ + 1) % capacity_;
    }

    count_ -= readable;
    return static_cast<std::int64_t>(readable);
}

void Pipe::close_read() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    read_closed_ = true;
}

void Pipe::close_write() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    write_closed_ = true;
}

bool Pipe::is_read_closed() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return read_closed_;
}

bool Pipe::is_write_closed() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return write_closed_;
}

std::size_t Pipe::bytes_available() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

std::size_t Pipe::space_available() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_ - count_;
}

} // namespace winios::kernel
