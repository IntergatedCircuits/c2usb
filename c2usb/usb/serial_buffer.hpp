// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace usb
{
template <typename T, typename TSize = size_t>
class double_buffer
{
  public:
    using size_type = TSize;
    constexpr double_buffer(const std::span<T>& space)
        : buffer_{space.data()}, size_{space.size() / 2}
    {}

  private:
    T* const buffer_{};
    const size_type size_{};

  protected:
    [[nodiscard]] T* buffer() const { return buffer_; }
    [[nodiscard]] T* buffer_of(size_type i)
    {
        return buffer_ + (i * size_); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    [[nodiscard]] auto size() const { return size_; }
};

class serial_rx_buffer : public double_buffer<uint8_t>
{
    using base = double_buffer<uint8_t>;

  protected:
    using base::buffer;
    using base::buffer_of;
    using base::size;
    using base::size_type;

    // NOLINTBEGIN(*-non-private-member-variables-in-classes)
    std::atomic<size_type> consume_pos_{0};
    std::array<std::atomic<size_type>, 2> produce_pos_{};
    // NOLINTEND(*-non-private-member-variables-in-classes)

  public:
    using base::base;
    void reset()
    {
        consume_pos_.store(0);
        produce_pos_[0].store(0);
        produce_pos_[1].store(0);
    }
    size_type read(uint8_t* data, size_type length);
    std::span<uint8_t> empty_side();
    bool set_produced(const std::span<uint8_t>& data);
};

class serial_tx_buffer : public double_buffer<uint8_t>
{
    using base = double_buffer<uint8_t>;
    using base::buffer;
    using base::buffer_of;
    using base::size;
    using base::size_type;

    static constexpr size_type consuming_flag = (std::numeric_limits<size_type>::max() / 2) + 1;
    std::array<std::atomic<size_type>, 2> pos_{};

  public:
    using base::base;
    void reset()
    {
        pos_[0].store(0);
        pos_[1].store(0);
    }
    void cancel_consume(const std::span<const uint8_t>& data);
    std::optional<std::span<const uint8_t>> write(const uint8_t* data, size_t* length);
    std::optional<std::span<const uint8_t>> advance(const std::span<const uint8_t>& data,
                                                    bool needs_zlp);
};

} // namespace usb
