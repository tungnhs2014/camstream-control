#include "camstream/mapped_buffer.hpp"

#include <sys/mman.h>
#include <utility>

namespace camstream {

MappedBuffer::MappedBuffer(std::uint32_t v4l2_buffer_index, void* mapping_address, std::size_t mapping_length) noexcept
    : buffer_index(v4l2_buffer_index), mapped_address(mapping_address), mapped_length(mapping_length),
      owns_mapping(true) {}

MappedBuffer::~MappedBuffer() {
    (void)unmap();
}

MappedBuffer::MappedBuffer(MappedBuffer&& other) noexcept
    : buffer_index(std::exchange(other.buffer_index, 0)), mapped_address(std::exchange(other.mapped_address, nullptr)),
      mapped_length(std::exchange(other.mapped_length, 0)), owns_mapping(std::exchange(other.owns_mapping, false)) {}

MappedBuffer& MappedBuffer::operator=(MappedBuffer&& other) noexcept {
    if (this != &other) {
        std::swap(buffer_index, other.buffer_index);
        std::swap(mapped_address, other.mapped_address);
        std::swap(mapped_length, other.mapped_length);
        std::swap(owns_mapping, other.owns_mapping);
    }
    return *this;
}

bool MappedBuffer::unmap() noexcept {
    if (!owns_mapping) {
        return true;
    }

    if (munmap(mapped_address, mapped_length) == -1) {
        return false;
    }

    mapped_address = nullptr;
    mapped_length = 0;
    owns_mapping = false;
    return true;
}

const std::byte* MappedBuffer::data() const noexcept {
    return owns_mapping ? static_cast<const std::byte*>(mapped_address) : nullptr;
}

std::uint32_t MappedBuffer::index() const noexcept {
    return buffer_index;
}

std::size_t MappedBuffer::length() const noexcept {
    return mapped_length;
}

} // namespace camstream
