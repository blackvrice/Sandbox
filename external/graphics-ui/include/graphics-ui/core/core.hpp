#pragma once

#include <graphics-ui/core/version.hpp>

#include <cstddef>
#include <expected>
#include <memory_resource>
#include <span>

namespace graphics_ui::core {

enum class ErrorCode {
    invalid_argument,
    out_of_memory,
    wrong_thread,
    stale_handle,
    closed,
    limit_exceeded,
    dependency_cycle,
    evaluation_failed,
    busy,
    queue_full,
};

template<class T>
using Result = std::expected<T, ErrorCode>;

class ByteBuffer {
public:
    // The supplied resource must outlive every buffer allocated through it.
    [[nodiscard]] static Result<ByteBuffer> create(
        std::size_t size,
        std::size_t alignment = alignof(std::max_align_t),
        std::pmr::memory_resource& resource = *std::pmr::new_delete_resource());

    ByteBuffer(const ByteBuffer&) = delete;
    ByteBuffer& operator=(const ByteBuffer&) = delete;
    ByteBuffer(ByteBuffer&& other) noexcept;
    ByteBuffer& operator=(ByteBuffer&& other) noexcept;
    ~ByteBuffer() noexcept;

    [[nodiscard]] std::span<std::byte> bytes() noexcept { return bytes_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }

private:
    ByteBuffer(std::span<std::byte> bytes, std::size_t alignment, std::pmr::memory_resource& resource) noexcept;
    void release() noexcept;

    std::span<std::byte> bytes_;
    std::size_t alignment_;
    std::pmr::memory_resource* resource_;
};

} // namespace graphics_ui::core
