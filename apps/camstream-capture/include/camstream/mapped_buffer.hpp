#ifndef CAMSTREAM_MAPPED_BUFFER_HPP
#define CAMSTREAM_MAPPED_BUFFER_HPP

#include <cstddef>
#include <cstdint>

namespace camstream {

/**
 * @brief Owns exactly one successful V4L2 MMAP mapping.
 *
 * Copying is prohibited. Moving transfers ownership without duplicating it,
 * and destruction releases any mapping still owned. Explicit unmap is
 * idempotent and allows callers to observe cleanup failures.
 */
class MappedBuffer final {
  public:
    /**
     * @brief Takes ownership of a successful mapping.
     * @param v4l2_buffer_index V4L2 buffer index associated with the mapping.
     * @param mapping_address Address returned by a successful mmap() call.
     * @param mapping_length Exact mapped length returned by VIDIOC_QUERYBUF.
     */
    MappedBuffer(std::uint32_t v4l2_buffer_index, void* mapping_address, std::size_t mapping_length) noexcept;
    ~MappedBuffer();

    MappedBuffer(const MappedBuffer&) = delete;
    MappedBuffer& operator=(const MappedBuffer&) = delete;
    MappedBuffer(MappedBuffer&& other) noexcept;
    MappedBuffer& operator=(MappedBuffer&& other) noexcept;

    /**
     * @brief Releases the owned mapping exactly once.
     * @return true when no mapping is owned or munmap() succeeds.
     */
    bool unmap() noexcept;

    /**
     * @brief Provides read-only access to the currently owned mapping.
     *
     * The pointer remains valid only while this object owns the mapping. V4L2
     * callers must additionally access payload bytes only after DQBUF transfers
     * the buffer to userspace and before QBUF returns it to the driver.
     *
     * @return Mapping address, or nullptr after ownership has been released.
     */
    const std::byte* data() const noexcept;

    std::uint32_t index() const noexcept;
    std::size_t length() const noexcept;

  private:
    std::uint32_t buffer_index = 0;
    void* mapped_address = nullptr;
    std::size_t mapped_length = 0;
    bool owns_mapping = false;
};

} // namespace camstream

#endif
