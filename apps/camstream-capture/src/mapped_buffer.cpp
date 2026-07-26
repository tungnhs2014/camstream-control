#include "camstream/mapped_buffer.hpp"

#include <sys/mman.h>
#include <utility>

namespace camstream {

MappedBuffer::MappedBuffer(std::uint32_t index, void* address,
                           std::size_t length) noexcept
    : index_(index)
    , address_(address)
    , length_(length)
    , owns_mapping_(true)
{
}

MappedBuffer::~MappedBuffer()
{
    (void)unmap();
}

MappedBuffer::MappedBuffer(MappedBuffer&& other) noexcept
    : index_(std::exchange(other.index_, 0))
    , address_(std::exchange(other.address_, nullptr))
    , length_(std::exchange(other.length_, 0))
    , owns_mapping_(std::exchange(other.owns_mapping_, false))
{
}

MappedBuffer& MappedBuffer::operator=(MappedBuffer&& other) noexcept
{
    if (this != &other) {
        std::swap(index_, other.index_);
        std::swap(address_, other.address_);
        std::swap(length_, other.length_);
        std::swap(owns_mapping_, other.owns_mapping_);
    }
    return *this;
}

bool MappedBuffer::unmap() noexcept
{
    if (!owns_mapping_) {
        return true;
    }

    if (munmap(address_, length_) == -1) {
        return false;
    }

    address_ = nullptr;
    length_ = 0;
    owns_mapping_ = false;
    return true;
}

const std::byte* MappedBuffer::data() const noexcept
{
    return owns_mapping_ ? static_cast<const std::byte*>(address_) : nullptr;
}

std::uint32_t MappedBuffer::index() const noexcept
{
    return index_;
}

std::size_t MappedBuffer::length() const noexcept
{
    return length_;
}

} // namespace camstream
