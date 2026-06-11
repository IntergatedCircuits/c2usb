// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cassert>
#include "usb/df/class/cdc.hpp"

namespace usb::cdc::ncm
{
// These definitions aren't meant to represent the standard definitions, but rather
// to provide a portable (device/host role independent) way to manage NTBs.

/// @brief  Verify the validity of an NTB, checking basic format requirements,
///         and ensuring that all datagram pointers and lengths are within the bounds of the NTB
/// @tparam BIT_SIZE: the bit size of the NTB format (16 or 32)
/// @param  ntb: the NTB data to validate
/// @return true if the NTB is valid, false otherwise
template <size_t BIT_SIZE>
bool is_valid_ntb(std::span<const uint8_t> ntb)
{
    if (ntb.size() < ntb::min_size<BIT_SIZE>())
    {
        return false;
    }
    // verify the header first
    auto* hdr = std_layout_cast<const ntb::header<BIT_SIZE>*>(ntb.data());
    if (not hdr->is_signature_valid() or (hdr->BlockLength > ntb.size()) or
        (hdr->HeaderLength != sizeof(*hdr)) or (hdr->BlockLength % sizeof(uint32_t) != 0))
    {
        return false;
    }

    // very rough estimate, but sufficient to avoid excessive loops on malformed NTBs
    const size_t max_dg_count = ntb.size() / usb::cdc::ncm::DATAGRAM_MIN_LENGTH;

    size_t dg_count = 0;
    uint16_t ndp_idx = hdr->NdpIndex;
    auto* dpt = std_layout_cast<const ntb::datagram_pointer_table<BIT_SIZE>*>(ntb.data() + ndp_idx);

    // verify all tables
    while ((ndp_idx >= sizeof(*hdr)) and ((ndp_idx + sizeof(*dpt)) < ntb.size()) and
           dpt->is_signature_valid() and
           (dpt->Length >= (sizeof(*dpt) + sizeof(ntb::datagram_ptr<BIT_SIZE>))) and
           ((ndp_idx + dpt->Length) <= ntb.size()))
    {
        // verify all datagram pointers in the table
        size_t dg_idx = 0;
        for (; dg_idx < (dpt->Length - sizeof(*dpt)) / sizeof(ntb::datagram_ptr<BIT_SIZE>);
             ++dg_idx)
        {
            auto& dg = dpt->Datagram[dg_idx];
            dg_count++;
            // datagram bounds check
            if ((dg.DatagramIndex < sizeof(*hdr)) or
                ((dg.DatagramIndex + dg.DatagramLength) > ntb.size()) or
                (dg.DatagramLength < DATAGRAM_MIN_LENGTH))
            {
                return false;
            }
            // datagram count overflow (e.g. through loops) check
            if (dg_count > max_dg_count)
            {
                return false;
            }
        }
        // the last datagram pointer must be null
        if ((dpt->Datagram[dg_idx].DatagramIndex | dpt->Datagram[dg_idx].DatagramLength) != 0)
        {
            return false;
        }
        ndp_idx = dpt->NextNdpIndex;
        // indicates last NDP
        if (ndp_idx == 0)
        {
            return true;
        }

        dpt = std_layout_cast<const ntb::datagram_pointer_table<BIT_SIZE>*>(ntb.data() + ndp_idx);
    }
    return false;
}

/// @brief  Base context for dual-buffered NTB management
/// @tparam BIT_SIZE: the bit size of the NTB format (16 or 32)
template <size_t BIT_SIZE>
struct ntb_ctx
{
    static_assert((BIT_SIZE == 16) or (BIT_SIZE == 32),
                  "Only 16 and 32 bit NTB formats are defined");
    using size_type = bitfilled::sized_unsigned_t<BIT_SIZE / 8>;
    using page_type = uint8_t;

    const std::span<uint32_t> buf;
    page_type page{};

    constexpr explicit ntb_ctx(std::span<uint32_t> buffer)
        : buf(buffer)
    {}

    [[nodiscard]] uint32_t size() const { return buf.size_bytes() / 2; }

    [[nodiscard]] constexpr int max_datagram_size() const
    {
        return int(size()) -
               int(sizeof(ntb::header<BIT_SIZE>) + sizeof(ntb::datagram_pointer_table<BIT_SIZE>) +
                   sizeof(ntb::datagram_ptr<BIT_SIZE>));
    }
    [[nodiscard]] constexpr size_t max_datagram_count() const
    {
        return size() / usb::cdc::ncm::DATAGRAM_MIN_LENGTH;
    }

    [[nodiscard]] constexpr bool buffer_valid() const
    {
        return max_datagram_size() >= int(DATAGRAM_MIN_LENGTH);
    }

    [[nodiscard]] uint8_t* data(page_type side) const
    {
        return std_layout_cast<uint8_t*>(buf.data()) + (side * size());
    }
    [[nodiscard]] ntb::header<BIT_SIZE>* header(page_type side) const
    {
        return std_layout_cast<ntb::header<BIT_SIZE>*>(data(side));
    }
};

/// @brief  Context for dual-buffered NTB assembly and transmission
/// @tparam BIT_SIZE: the bit size of the NTB format (16 or 32)
template <size_t BIT_SIZE>
struct ntb_tx_ctx : public ntb_ctx<BIT_SIZE>
{
    using base = ntb_ctx<BIT_SIZE>;
    using base::base;
    using base::data;
    using base::size;
    using size_type = typename base::size_type;

  private:
    // reflect the layout of a datagram pointer
    struct dg_size_store
    {
        size_type size{};
        size_type reserved{};
    };
    static_assert(sizeof(dg_size_store) == sizeof(ntb::datagram_ptr<BIT_SIZE>));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    [[nodiscard]] dg_size_store* dg_size_ptr() const
    {
        // datagram lengths are saved backwards starting from the end of the buffer
        // and get ordered only at the time of NTB transmission
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<dg_size_store*>(std_layout_cast<uint8_t*>(this->data(this->page)) +
                                                this->size() -
                                                (sizeof(dg_size_store) * this->dg_count));
    }
    [[nodiscard]] dg_size_store* dg_size_end_ptr() const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<dg_size_store*>(this->data(this->page) + this->size() -
                                                sizeof(dg_size_store));
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
#pragma GCC diagnostic pop

  public:
    uint32_t max_size{};
    size_type rem_size{};
    size_type index{};
    bitfilled::sized_unsigned_t<BIT_SIZE / 16> dg_count{};
    bitfilled::sized_unsigned_t<BIT_SIZE / 16> dg_max_limit{};

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    [[nodiscard]] uint8_t* tail() const { return this->data(this->page) + this->index; }

    /// @brief  Initialize the context for populating a new NTB,
    ///         called at start and after popping an NTB for transmission
    void init()
    {
        dg_count = 0;
        index = sizeof(ntb::header<BIT_SIZE>);
        rem_size = std::min(this->size(), max_size) - sizeof(ntb::header<BIT_SIZE>) -
                   sizeof(ntb::datagram_pointer_table<BIT_SIZE>);
    }

    /// @brief  Append a datagram to the current NTB being assembled
    /// @tparam TIterator: an input iterator type with value type convertible to uint8_t
    /// @param  begin: the beginning of the datagram data
    /// @param  end: the end of the datagram data
    /// @return true if the datagram was successfully appended, false if it did not fit
    template <typename TIterator>
    bool append_datagram(TIterator begin, TIterator end)
        requires(std::is_convertible_v<
                 std::decay_t<typename std::iterator_traits<TIterator>::value_type>, uint8_t>)
    {
        auto sz = std::distance(begin, end);
        size_t word_size = c2usb::aligned_size<uint32_t>(static_cast<size_t>(sz));
        size_t add_size = word_size + sizeof(ntb::datagram_ptr<BIT_SIZE>);

        if ((sz < static_cast<decltype(sz)>(DATAGRAM_MIN_LENGTH)) or (add_size > this->rem_size) or
            (this->dg_max_limit and (this->dg_count >= this->dg_max_limit)))
        {
            return false;
        }
        auto* buf_ptr = this->tail();
        std::copy(begin, end, buf_ptr);

        this->index += word_size;
        this->rem_size -= add_size;
        this->dg_count++;
        dg_size_ptr()->size = static_cast<size_type>(sz);
        return true;
    }

    /// @brief  Allocate space for a datagram to be filled in-place, and to be committed with
    ///         commit_datagram()
    /// @param  sz: datagram payload size in bytes
    /// @return writable span of the reserved area, or empty on failure
    [[nodiscard]] std::span<uint8_t> allocate_datagram(size_t sz) const
    {
        size_t word_size = c2usb::aligned_size<uint32_t>(sz);
        size_t add_size = word_size + sizeof(ntb::datagram_ptr<BIT_SIZE>);

        if ((sz < DATAGRAM_MIN_LENGTH) or (add_size > size_t(this->rem_size)) or
            (this->dg_max_limit and (this->dg_count >= this->dg_max_limit)))
        {
            return {};
        }

        return std::span<uint8_t>(tail(), sz);
    }

    /// @brief  Finalize a datagram allocated with allocate_datagram(), making it part of the NTB
    /// @param  dg: the datagram span returned by the last allocate_datagram() call
    void commit_datagram(const std::span<uint8_t>& dg)
    {
        size_t word_size = c2usb::aligned_size<uint32_t>(dg.size());
        size_t add_size = word_size + sizeof(ntb::datagram_ptr<BIT_SIZE>);

        assert((dg.size() >= DATAGRAM_MIN_LENGTH) and (dg.data() == tail()) and
               (add_size <= this->rem_size) and
               (not this->dg_max_limit or (this->dg_count < this->dg_max_limit)));

        this->index += static_cast<size_type>(word_size);
        this->rem_size -= static_cast<size_type>(add_size);
        this->dg_count++;
        dg_size_ptr()->size = static_cast<size_type>(dg.size());
    }

    /// @brief  Finalize the current NTB and prepare it for transmission,
    ///         returning a span of the NTB data to be sent
    /// @return a span of the NTB ready for transmission
    std::span<const uint8_t> pop_ntb()
    {
        if (dg_count == 0)
        {
            return {};
        }

        // layout while populating:
        // +--------------------------------------------------------------------------------------+
        // | header | datagram 0 | ... | datagram N | empty         | reverse datagram size store |
        // +--------------------------------------------------------------------------------------+
        //
        // the below transformation results in:
        // +--------------------------------------------------------------------------------------+
        // | header | datagram 0 | ... | datagram N | datagram pointer table | empty (not sent)   |
        // +--------------------------------------------------------------------------------------+
        // (note the overlap between reverse datagram size store and datagram pointer table)

        // place the datagram pointer table right after the last datagram
        auto* dpt = std_layout_cast<ntb::datagram_pointer_table<BIT_SIZE>*>(tail());
        dpt->Signature = dpt->valid_signature();
        dpt->Length = sizeof(*dpt) + (sizeof(ntb::datagram_ptr<BIT_SIZE>) * dg_count);
        dpt->NextNdpIndex = 0; // no further tables

        // fill the header fields
        auto* hdr = this->header(this->page);
        hdr->Signature = hdr->valid_signature();
        hdr->HeaderLength = sizeof(*hdr);
        hdr->NdpIndex = index;
        hdr->BlockLength = index + dpt->Length;

        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

        // round 1: copy datagram lengths from the so-far alternatively used Index fields
        auto* dg = dpt->Datagram;
        auto* rev_dg = dg_size_end_ptr();
        for (int i = 0; i < dg_count; i++)
        {
            dg[i].DatagramLength = rev_dg[-i].size;
        }
        // round 2: populate Index fields based on datagram lengths
        dg[0].DatagramIndex = sizeof(*hdr);
        for (int i = 0; i < (dg_count - 1); ++i)
        {
            dg[i + 1].DatagramIndex =
                c2usb::aligned_size<uint32_t>(dg[i].DatagramIndex + dg[i].DatagramLength);
        }
        // a null element ends the datagram pointer table
        dg[dg_count] = {};

        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

        // switch to the other page for consecutive datagrams
        this->page = !this->page;
        this->header(this->page)->Sequence = hdr->Sequence + 1;
        init();

        return std::span<const uint8_t>(std_layout_cast<const uint8_t*>(this->data(!this->page)),
                                        hdr->BlockLength);
    }

    /// @brief  Performs the SET_NTB_INPUT_SIZE request, validating and applying it
    /// @param  msg: the control message containing the new input size
    /// @return True if the input size was successfully set, false otherwise
    bool set_input_size(const std::span<const uint8_t>& msg)
    {
        auto ntb_size = *std_layout_cast<const ntb::input_size*>(msg.data());

        // request size check
        if ((msg.size() != sizeof(ntb_size.dwNtbInMaxSize)) and (msg.size() != sizeof(ntb_size)))
        {
            return false;
        }
        // dwNtbInMaxSize must be within bounds
        if ((ntb_size.dwNtbInMaxSize <=
             (sizeof(ntb::header<BIT_SIZE>) + sizeof(ntb::datagram_pointer_table<BIT_SIZE>) +
              DATAGRAM_MIN_LENGTH)) or
            (ntb_size.dwNtbInMaxSize > this->size()))
        {
            return false;
        }

        this->max_size = ntb_size.dwNtbInMaxSize;

        // if the request includes the wNtbInMaxDatagrams field, apply it
        if (msg.size() == sizeof(ntb_size))
        {
            if (ntb_size.wNtbInMaxDatagrams == 0)
            {
                this->dg_max_limit = 0;
            }
            else
            {
                this->dg_max_limit =
                    std::min<uint16_t>(std::numeric_limits<decltype(this->dg_max_limit)>::max(),
                                       ntb_size.wNtbInMaxDatagrams);
            }
        }
        return true;
    }
};

/// @brief  Context for dual-buffered NTB reception and datagram extraction
/// @tparam BIT_SIZE: the bit size of the NTB format (16 or 32)
template <size_t BIT_SIZE>
struct ntb_rx_ctx : public ntb_ctx<BIT_SIZE>
{
  private:
    using base = ntb_ctx<BIT_SIZE>;

  public:
    using base::base;
    using base::data;
    using base::size;
    using size_type = typename base::size_type;
    std::array<size_type, 2> length{};
    const usb::cdc::ncm::ntb::header<BIT_SIZE>* hdr{};
    const usb::cdc::ncm::ntb::datagram_pointer_table<BIT_SIZE>* dpt{};
    size_type table_index{};

    [[nodiscard]] std::span<uint8_t> span(base::page_type side) const
    {
        return std::span<uint8_t>(data(side), length[side]);
    }

    /// @brief  Initialize the context for processing a newly received NTB,
    ///         called when a new NTB is received and validated
    /// @param  data: pointer to the beginning of the NTB data
    void set_header(const uint8_t* data)
    {
        using namespace usb::cdc::ncm;
        hdr = std_layout_cast<const ntb::header<BIT_SIZE>*>(data);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        dpt = std_layout_cast<const ntb::datagram_pointer_table<BIT_SIZE>*>(data + hdr->NdpIndex);
        table_index = std::numeric_limits<decltype(table_index)>::max(); // overflow on increment
    }

    /// @brief  Extract the next datagram from the current NTB being processed,
    ///         returning a span of the datagram data
    /// @return the next datagram data, or empty
    std::span<const uint8_t> pop_datagram()
    {
        if (not advance_datagram())
        {
            return {};
        }
        return std::span<const uint8_t>(std_layout_cast<const uint8_t*>(this->data(this->page)) +
                                            dpt->Datagram[table_index].DatagramIndex,
                                        dpt->Datagram[table_index].DatagramLength);
    }

    /// @brief  Advance to the next datagram in the current buffer page, if any
    /// @return true if successful, false if there are no more datagrams left
    bool advance_datagram()
    {
        // advance to the next datagram in the pointer table
        // table_index starts at max() so that the first increment wraps to 0
        if (size_type next_index = table_index + size_type(1);
            dpt->Datagram[next_index].DatagramIndex != 0)
        {
            table_index = next_index;
            return true;
        }
        // advance to the next datagram pointer table
        if (dpt->NextNdpIndex != 0)
        {
            dpt = std_layout_cast<const ntb::datagram_pointer_table<BIT_SIZE>*>(
                std_layout_cast<const uint8_t*>(hdr) + dpt->NextNdpIndex);
            table_index = 0;
            return true;
        }
        // advance to the next NTB if: block length is valid,
        // enough length left in the buffer, and the next NTB checks out as valid
        const uint8_t* next_hdr = std_layout_cast<const uint8_t*>(hdr) + hdr->BlockLength;
        auto rem_len =
            length[this->page] - std::distance<const uint8_t*>(this->data(this->page), next_hdr);
        if ((hdr->BlockLength >= ntb::min_size<BIT_SIZE>()) &&
            (rem_len >= int(ntb::min_size<BIT_SIZE>())) &&
            is_valid_ntb<BIT_SIZE>(std::span<const uint8_t>(next_hdr, size_t(rem_len))))
        {
            set_header(next_hdr);
            return true;
        }
        return false;
    }
};

} // namespace usb::cdc::ncm
