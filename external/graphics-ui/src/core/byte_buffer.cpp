#include <graphics-ui/core/core.hpp>

#include <algorithm>
#include <bit>
#include <new>
#include <utility>

namespace graphics_ui::core {

Result<ByteBuffer> ByteBuffer::create(std::size_t size, std::size_t alignment, std::pmr::memory_resource& resource) {
    if (size == 0 || !std::has_single_bit(alignment))
        return std::unexpected(ErrorCode::invalid_argument);

    try {
        auto* storage = static_cast<std::byte*>(resource.allocate(size, alignment));
        std::fill_n(storage, size, std::byte{0});
        return ByteBuffer({storage, size}, alignment, resource);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

ByteBuffer::ByteBuffer(std::span<std::byte> bytes, std::size_t alignment, std::pmr::memory_resource& resource) noexcept
    : bytes_(bytes), alignment_(alignment), resource_(&resource) {}

ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept
    : bytes_(std::exchange(other.bytes_, {})), alignment_(other.alignment_), resource_(other.resource_) {}

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept {
    if (this == &other) return *this;

    release();
    bytes_ = std::exchange(other.bytes_, {});
    alignment_ = other.alignment_;
    resource_ = other.resource_;
    return *this;
}

ByteBuffer::~ByteBuffer() noexcept {
    release();
}

void ByteBuffer::release() noexcept {
    if (!bytes_.empty())
        resource_->deallocate(bytes_.data(), bytes_.size(), alignment_);
}

} // namespace graphics_ui::core
