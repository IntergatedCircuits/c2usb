// SPDX-License-Identifier: MPL-2.0
#include "usb/class/cdc_ncm_ntb.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <vector>
#include "test_framework.hpp"

// ---------------------------------------------------------------------------
// NTB byte-level builder helpers
// ---------------------------------------------------------------------------
//
// CDC NCM 1.0 on-wire layouts:
//
// NTB-16 header (NTH16) - 12 bytes:
//   Signature    [0..3]  "NCMH"
//   HeaderLength [4..5]  = 12 (0x000C)
//   Sequence     [6..7]
//   BlockLength  [8..9]  (total NTB size, must be multiple of 4)
//   NdpIndex     [10..11] (offset of first NDP, must be >= 12)
//
// NTB-32 header (NTH32) - 16 bytes:
//   Signature    [0..3]  "ncmh"
//   HeaderLength [4..5]  = 16 (0x0010)
//   Sequence     [6..7]
//   BlockLength  [8..11]
//   NdpIndex     [12..15]
//
// NDP-16 - base 8 bytes then datagram_ptr entries (each 4 bytes):
//   Signature    [0..3]  "NCM0"
//   Length       [4..5]  (total NDP size including null terminator)
//   NextNdpIndex [6..7]  (0 = last)
//   Datagram[0]  [8..11] (first datagram_ptr: Index[2]+Length[2])
//   ...
//   Datagram[N]  = {0,0}  (null terminator)
//
//   sizeof(datagram_pointer_table<16>) == 12  (base 8 + one Datagram entry 4)
//   "ndp_base_size" (offset of Datagram[0]) = 8
//
// NDP-32 - base 16 bytes then datagram_ptr entries (each 8 bytes):
//   Signature    [0..3]  "ncm0"
//   Length       [4..5]
//   Reserved     [6..7]  (reserved_t<2>)
//   NextNdpIndex [8..11]
//   Reserved     [12..15] (reserved_t<4>)
//   Datagram[0]  [16..23]
//   ...
//
//   sizeof(datagram_pointer_table<32>) == 24  (base 16 + one Datagram entry 8)
//   "ndp_base_size" (offset of Datagram[0]) = 16
//
// DATAGRAM_MIN_LENGTH = 14
//
// ---------------------------------------------------------------------------

namespace
{

template <typename T>
void write_le(uint8_t* p, T val)
{
    static_assert(std::is_unsigned_v<T>);
    for (size_t i = 0; i < sizeof(T); ++i)
    {
        p[i] = static_cast<uint8_t>(val & 0xFF);
        val >>= 8;
    }
}

template <size_t BIT_SIZE>
struct ntb_traits;

template <>
struct ntb_traits<16>
{
    using index_t = uint16_t;
    static constexpr size_t header_size = 12;
    static constexpr size_t ndp_index_offset = 10;
    // Offset of Datagram[0] within the NDP structure
    static constexpr size_t ndp_datagram_offset = 8;
    static constexpr size_t datagram_ptr_size = 4;

    static void write_header(uint8_t* buf, uint16_t seq, index_t block_len, index_t ndp_idx)
    {
        buf[0] = 'N';
        buf[1] = 'C';
        buf[2] = 'M';
        buf[3] = 'H';
        write_le<uint16_t>(buf + 4, static_cast<uint16_t>(header_size));
        write_le<uint16_t>(buf + 6, seq);
        write_le<uint16_t>(buf + 8, static_cast<uint16_t>(block_len));
        write_le<uint16_t>(buf + 10, static_cast<uint16_t>(ndp_idx));
    }

    // Write an NDP at offset `off`.
    // `dgs` = list of {DatagramIndex, DatagramLength} pairs, terminator added automatically.
    // Returns number of bytes written (== NDP Length field value).
    static size_t write_ndp(uint8_t* buf, size_t off, index_t next_ndp,
                            std::span<const std::pair<index_t, index_t>> dgs)
    {
        uint16_t n = static_cast<uint16_t>(dgs.size());
        uint16_t length = static_cast<uint16_t>(
            ndp_datagram_offset + datagram_ptr_size * (n + 1)); // +1 for null terminator
        uint8_t* p = buf + off;
        p[0] = 'N';
        p[1] = 'C';
        p[2] = 'M';
        p[3] = '0';
        write_le<uint16_t>(p + 4, length);
        write_le<uint16_t>(p + 6, static_cast<uint16_t>(next_ndp)); // NextNdpIndex
        // Datagram[0] starts at offset 8
        size_t dp_off = ndp_datagram_offset;
        for (auto& [idx, len] : dgs)
        {
            write_le<uint16_t>(p + dp_off, static_cast<uint16_t>(idx));
            write_le<uint16_t>(p + dp_off + 2, static_cast<uint16_t>(len));
            dp_off += datagram_ptr_size;
        }
        // null terminator
        write_le<uint16_t>(p + dp_off, 0);
        write_le<uint16_t>(p + dp_off + 2, 0);
        return length;
    }
};

template <>
struct ntb_traits<32>
{
    using index_t = uint32_t;
    static constexpr size_t header_size = 16;
    static constexpr size_t ndp_index_offset = 12;
    // Offset of Datagram[0] within the NDP-32 structure
    // Layout: sig[4]+Len[2]+Reserved[2]+Next[4]+Reserved[4] = 16
    static constexpr size_t ndp_datagram_offset = 16;
    static constexpr size_t datagram_ptr_size = 8;

    static void write_header(uint8_t* buf, uint16_t seq, index_t block_len, index_t ndp_idx)
    {
        buf[0] = 'n';
        buf[1] = 'c';
        buf[2] = 'm';
        buf[3] = 'h';
        write_le<uint16_t>(buf + 4, static_cast<uint16_t>(header_size));
        write_le<uint16_t>(buf + 6, seq);
        write_le<uint32_t>(buf + 8, block_len);
        write_le<uint32_t>(buf + 12, ndp_idx);
    }

    static size_t write_ndp(uint8_t* buf, size_t off, index_t next_ndp,
                            std::span<const std::pair<index_t, index_t>> dgs)
    {
        uint32_t n = static_cast<uint32_t>(dgs.size());
        uint16_t length = static_cast<uint16_t>(ndp_datagram_offset + datagram_ptr_size * (n + 1));
        uint8_t* p = buf + off;
        p[0] = 'n';
        p[1] = 'c';
        p[2] = 'm';
        p[3] = '0';
        write_le<uint16_t>(p + 4, length);
        write_le<uint16_t>(p + 6, 0);        // Reserved6 (2 bytes)
        write_le<uint32_t>(p + 8, next_ndp); // NextNdpIndex
        write_le<uint32_t>(p + 12, 0);       // Reserved12 (4 bytes)
        // Datagram[0] starts at offset 16
        size_t dp_off = ndp_datagram_offset;
        for (auto& [idx, len] : dgs)
        {
            write_le<uint32_t>(p + dp_off, idx);
            write_le<uint32_t>(p + dp_off + 4, len);
            dp_off += datagram_ptr_size;
        }
        // null terminator
        write_le<uint32_t>(p + dp_off, 0);
        write_le<uint32_t>(p + dp_off + 4, 0);
        return length;
    }
};

// Minimum datagram size per spec
static constexpr size_t DG_MIN = 14;
// Datagram payload used in builders (>= DG_MIN, 4-byte aligned for convenience)
static constexpr size_t DG_SIZE = 20;

// ---- Static layout assertions -------------------------------------------

static_assert(sizeof(usb::cdc::ncm::ntb::header<16>) == 12);
static_assert(sizeof(usb::cdc::ncm::ntb::header<32>) == 16);
static_assert(sizeof(usb::cdc::ncm::ntb::datagram_pointer_table<16>) == 12);
static_assert(sizeof(usb::cdc::ncm::ntb::datagram_pointer_table<32>) == 24);
static_assert(sizeof(usb::cdc::ncm::ntb::datagram_ptr<16>) == 4);
static_assert(sizeof(usb::cdc::ncm::ntb::datagram_ptr<32>) == 8);
// Verify our Datagram offset constants match the struct layout
static_assert(offsetof(usb::cdc::ncm::ntb::datagram_pointer_table<16>, Datagram) ==
              ntb_traits<16>::ndp_datagram_offset);
static_assert(offsetof(usb::cdc::ncm::ntb::datagram_pointer_table<32>, Datagram) ==
              ntb_traits<32>::ndp_datagram_offset);

// ---- NTB factory functions -----------------------------------------------

// Layout: | header | datagram | NDP |  (NDP at end)
template <size_t BIT_SIZE>
std::vector<uint8_t> make_ntb_ndp_at_end()
{
    using T = ntb_traits<BIT_SIZE>;
    using idx_t = typename T::index_t;

    idx_t dg_off = static_cast<idx_t>(T::header_size);
    idx_t ndp_off = static_cast<idx_t>(T::header_size + DG_SIZE);
    size_t ndp_len = T::ndp_datagram_offset + 2 * T::datagram_ptr_size; // 1 dg + terminator
    idx_t block_len = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp_off + ndp_len));

    std::vector<uint8_t> buf(block_len, 0);
    T::write_header(buf.data(), 0, block_len, ndp_off);

    std::pair<idx_t, idx_t> dg{dg_off, static_cast<idx_t>(DG_SIZE)};
    T::write_ndp(buf.data(), ndp_off, 0, std::span<const std::pair<idx_t, idx_t>>(&dg, 1));
    return buf;
}

// Layout: | header | NDP | datagram |  (NDP at front, right after header)
template <size_t BIT_SIZE>
std::vector<uint8_t> make_ntb_ndp_at_front()
{
    using T = ntb_traits<BIT_SIZE>;
    using idx_t = typename T::index_t;

    idx_t ndp_off = static_cast<idx_t>(T::header_size);
    size_t ndp_len = T::ndp_datagram_offset + 2 * T::datagram_ptr_size; // 1 dg + terminator
    idx_t dg_off = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp_off + ndp_len));
    idx_t block_len = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(dg_off + DG_SIZE));

    std::vector<uint8_t> buf(block_len, 0);
    T::write_header(buf.data(), 0, block_len, ndp_off);

    std::pair<idx_t, idx_t> dg{dg_off, static_cast<idx_t>(DG_SIZE)};
    T::write_ndp(buf.data(), ndp_off, 0, std::span<const std::pair<idx_t, idx_t>>(&dg, 1));
    return buf;
}

// Layout: | header | dg0 | NDP | dg1 |  (NDP in middle, NDP has two datagrams)
template <size_t BIT_SIZE>
std::vector<uint8_t> make_ntb_ndp_in_middle()
{
    using T = ntb_traits<BIT_SIZE>;
    using idx_t = typename T::index_t;

    idx_t dg0_off = static_cast<idx_t>(T::header_size);
    idx_t ndp_off = static_cast<idx_t>(dg0_off + DG_SIZE);
    size_t ndp_len = T::ndp_datagram_offset + 3 * T::datagram_ptr_size; // 2 dgs + terminator
    idx_t dg1_off = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp_off + ndp_len));
    idx_t block_len = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(dg1_off + DG_SIZE));

    std::vector<uint8_t> buf(block_len, 0);
    T::write_header(buf.data(), 0, block_len, ndp_off);

    std::array<std::pair<idx_t, idx_t>, 2> dgs{{
        {dg0_off, static_cast<idx_t>(DG_SIZE)},
        {dg1_off, static_cast<idx_t>(DG_SIZE)},
    }};
    T::write_ndp(buf.data(), ndp_off, 0, std::span<const std::pair<idx_t, idx_t>>(dgs));
    return buf;
}

// Layout: | header | dg0 | NDP0->NDP1 | dg1 |  (two chained NDPs, one datagram each)
template <size_t BIT_SIZE>
std::vector<uint8_t> make_ntb_two_ndps()
{
    using T = ntb_traits<BIT_SIZE>;
    using idx_t = typename T::index_t;

    size_t ndp_single_len = T::ndp_datagram_offset + 2 * T::datagram_ptr_size; // 1 dg + terminator

    idx_t dg0_off = static_cast<idx_t>(T::header_size);
    idx_t ndp0_off = static_cast<idx_t>(dg0_off + DG_SIZE);
    idx_t ndp1_off = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp0_off + ndp_single_len));
    idx_t dg1_off = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp1_off + ndp_single_len));
    idx_t block_len = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(dg1_off + DG_SIZE));

    std::vector<uint8_t> buf(block_len, 0);
    T::write_header(buf.data(), 0, block_len, ndp0_off);

    std::pair<idx_t, idx_t> dg0{dg0_off, static_cast<idx_t>(DG_SIZE)};
    T::write_ndp(buf.data(), ndp0_off, ndp1_off, std::span<const std::pair<idx_t, idx_t>>(&dg0, 1));

    std::pair<idx_t, idx_t> dg1{dg1_off, static_cast<idx_t>(DG_SIZE)};
    T::write_ndp(buf.data(), ndp1_off, 0, std::span<const std::pair<idx_t, idx_t>>(&dg1, 1));
    return buf;
}

// Layout: | header | dg0 | NDP0 | dg1 | NDP1 |, with NDP0 <-> NDP1 circularly
// referencing each other via NextNdpIndex (simulates a malicious/corrupt NTB
// attempting to trigger an infinite traversal loop, i.e. a DoS attack).
template <size_t BIT_SIZE>
std::vector<uint8_t> make_ntb_circular_ndps()
{
    using T = ntb_traits<BIT_SIZE>;
    using idx_t = typename T::index_t;

    size_t ndp_single_len = T::ndp_datagram_offset + 2 * T::datagram_ptr_size; // 1 dg + terminator

    idx_t dg0_off = static_cast<idx_t>(T::header_size);
    idx_t ndp0_off = static_cast<idx_t>(dg0_off + DG_SIZE);
    idx_t dg1_off = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp0_off + ndp_single_len));
    idx_t ndp1_off = static_cast<idx_t>(dg1_off + DG_SIZE);
    idx_t block_len = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp1_off + ndp_single_len));

    std::vector<uint8_t> buf(block_len, 0);
    T::write_header(buf.data(), 0, block_len, ndp0_off);

    std::pair<idx_t, idx_t> dg0{dg0_off, static_cast<idx_t>(DG_SIZE)};
    // NDP0 points forward to NDP1
    T::write_ndp(buf.data(), ndp0_off, ndp1_off, std::span<const std::pair<idx_t, idx_t>>(&dg0, 1));

    std::pair<idx_t, idx_t> dg1{dg1_off, static_cast<idx_t>(DG_SIZE)};
    // NDP1 points backward to NDP0, forming a cycle
    T::write_ndp(buf.data(), ndp1_off, ndp0_off, std::span<const std::pair<idx_t, idx_t>>(&dg1, 1));
    return buf;
}

// NDP with zero datagrams (only null terminator entry)
template <size_t BIT_SIZE>
std::vector<uint8_t> make_ntb_empty_ndp()
{
    using T = ntb_traits<BIT_SIZE>;
    using idx_t = typename T::index_t;

    idx_t ndp_off = static_cast<idx_t>(T::header_size);
    size_t ndp_len = T::ndp_datagram_offset + T::datagram_ptr_size; // 0 dgs + terminator
    idx_t block_len = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp_off + ndp_len));

    std::vector<uint8_t> buf(block_len, 0);
    T::write_header(buf.data(), 0, block_len, ndp_off);
    T::write_ndp(buf.data(), ndp_off, 0, std::span<const std::pair<idx_t, idx_t>>{});
    return buf;
}

// Helper: allocate an rx_ctx, copy `ntb` into page 0, set length[0], call set_header.
// Returns {ctx, storage} - storage must outlive the ctx.
template <size_t BIT_SIZE>
auto make_rx_ctx(const std::vector<uint8_t>& ntb)
{
    using ctx_t = usb::cdc::ncm::ntb_rx_ctx<BIT_SIZE>;
    size_t page_bytes = c2usb::aligned_size<uint32_t>(ntb.size());
    auto storage = std::make_shared<std::vector<uint32_t>>(page_bytes * 2 / sizeof(uint32_t), 0u);
    ctx_t ctx(std::span<uint32_t>(storage->data(), storage->size()));
    std::memcpy(ctx.data(false), ntb.data(), ntb.size());
    ctx.length[0] = static_cast<typename ctx_t::size_type>(ntb.size());
    ctx.set_header(ctx.data(false));
    return std::pair{ctx, storage};
}

// Helper: build a tx_ctx with two pages of `page_bytes` each, max_size set, init() called.
// Returns {ctx, storage} - storage must outlive the ctx.
template <size_t BIT_SIZE>
auto make_tx_ctx(size_t page_bytes)
{
    using ctx_t = usb::cdc::ncm::ntb_tx_ctx<BIT_SIZE>;
    page_bytes = c2usb::aligned_size<uint32_t>(page_bytes);
    auto storage = std::make_shared<std::vector<uint32_t>>(page_bytes * 2 / sizeof(uint32_t), 0u);
    ctx_t ctx(std::span<uint32_t>(storage->data(), storage->size()));
    ctx.max_size = static_cast<uint32_t>(page_bytes);
    ctx.init();
    return std::pair{ctx, storage};
}

// Helper: drain all datagrams from an rx_ctx into a vector of byte vectors.
template <size_t BIT_SIZE>
std::vector<std::vector<uint8_t>> drain_rx(usb::cdc::ncm::ntb_rx_ctx<BIT_SIZE>& ctx)
{
    std::vector<std::vector<uint8_t>> result;
    for (auto span = ctx.pop_datagram(); not span.empty(); span = ctx.pop_datagram())
    {
        result.emplace_back(span.begin(), span.end());
    }
    return result;
}

// ---------------------------------------------------------------------------
// ntb_tx_ctx armed state test helpers
// ---------------------------------------------------------------------------

using ntb16 = std::integral_constant<size_t, 16>;
using ntb32 = std::integral_constant<size_t, 32>;

constexpr size_t TX_PAGE_SIZE = 256;

// the start of the given page in a tx_ctx storage made with TX_PAGE_SIZE
const uint8_t* tx_page(const std::vector<uint32_t>& storage, size_t page)
{
    // NOLINTNEXTLINE(*-type-reinterpret-cast)
    return reinterpret_cast<const uint8_t*>(storage.data()) + (page * TX_PAGE_SIZE);
}

uint16_t ntb_sequence(std::span<const uint8_t> ntb)
{
    return static_cast<uint16_t>(ntb[6] | (ntb[7] << 8));
}

template <typename TCtx>
bool append_filled(TCtx& ctx, uint8_t fill)
{
    std::array<uint8_t, DG_SIZE> dg{};
    dg.fill(fill);
    return ctx.append_datagram(dg.begin(), dg.end());
}

template <size_t BIT_SIZE>
std::vector<std::vector<uint8_t>> rx_roundtrip(std::span<const uint8_t> ntb)
{
    std::vector<uint8_t> ntb_copy(ntb.begin(), ntb.end());
    auto [rx, rx_storage] = make_rx_ctx<BIT_SIZE>(ntb_copy);
    return drain_rx(rx);
}

} // namespace

SUITE(cdc_ncm_ntb)
{
    // =======================================================================
    // VALID NTBs
    // =======================================================================

    TEMPLATE_TEST_CASE("valid NTB: NDP at end", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("valid NTB: NDP at front", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_ndp_at_front<BIT_SIZE>();
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("valid NTB: NDP in middle with two datagrams", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_ndp_in_middle<BIT_SIZE>();
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("valid NTB: two chained NDPs", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_two_ndps<BIT_SIZE>();
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // Span larger than BlockLength is fine (host-side receive buffer scenario)
    TEMPLATE_TEST_CASE("valid NTB: span larger than BlockLength", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        buf.resize(buf.size() + 64, 0);
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // =======================================================================
    // INVALID NTBs
    // =======================================================================

    // --- Header signature ---------------------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: wrong header signature", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        buf[0] = 'X';
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEST_CASE("invalid: NTB16 buffer rejected as NTB32")
    {
        auto buf = make_ntb_ndp_at_end<16>();
        CHECK(
            not usb::cdc::ncm::is_valid_ntb<32>(std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEST_CASE("invalid: NTB32 buffer rejected as NTB16")
    {
        auto buf = make_ntb_ndp_at_end<32>();
        CHECK(
            not usb::cdc::ncm::is_valid_ntb<16>(std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // --- HeaderLength -------------------------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: wrong HeaderLength", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        // HeaderLength at [4..5]
        write_le<uint16_t>(buf.data() + 4, static_cast<uint16_t>(T::header_size - 2));
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // --- BlockLength alignment ----------------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: BlockLength not multiple of 4", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        using index_t = typename T::index_t;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        // Corrupt BlockLength at [8..] to be non-aligned; grow span to match
        auto bl = static_cast<index_t>(buf.size() + 1);
        buf.push_back(0);
        write_le<index_t>(buf.data() + 8, bl);
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // --- BlockLength vs span ------------------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: BlockLength exceeds span", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size() - 4)));
    };

    // --- NdpIndex location --------------------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: NdpIndex inside header (too small)", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        using index_t = typename T::index_t;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        // set NdpIndex to 4, which is < header_size
        write_le<index_t>(buf.data() + T::ndp_index_offset, 4);
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("invalid NTB: NDP extends beyond span", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        using index_t = typename T::index_t;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        // Point NdpIndex to the very last byte - NDP base won't fit
        write_le<index_t>(buf.data() + T::ndp_index_offset, static_cast<index_t>(buf.size() - 1));
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // --- NDP signature ------------------------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: wrong NDP signature", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        // NDP starts right after the header and the datagram
        buf[ntb_traits<BIT_SIZE>::header_size + DG_SIZE] = 'X';
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // --- NDP Length ---------------------------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: NDP Length too small", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        // NDP right after the datagram; Length at NDP+4
        // Minimum valid Length = ndp_datagram_offset + datagram_ptr_size
        // Write ndp_datagram_offset, which is below the minimum
        size_t ndp_off = T::header_size + DG_SIZE;
        write_le<uint16_t>(buf.data() + ndp_off + 4, static_cast<uint16_t>(T::ndp_datagram_offset));
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("invalid NTB: NDP Length makes NDP extend past BlockLength", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        auto buf = make_ntb_ndp_at_end<BIT_SIZE>();
        size_t ndp_off = T::header_size + DG_SIZE;
        auto big = static_cast<uint16_t>(buf.size() + T::datagram_ptr_size);
        write_le<uint16_t>(buf.data() + ndp_off + 4, big);
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // --- Datagram pointer checks --------------------------------------------

    // The "NDP at front" layout puts the NDP right after the header, so we
    // can easily address Datagram[0] and the terminator.

    TEMPLATE_TEST_CASE("invalid NTB: datagram DatagramIndex inside header", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        using index_t = typename T::index_t;
        auto buf = make_ntb_ndp_at_front<BIT_SIZE>();
        // NDP right after the header, Datagram[0] at NDP + ndp_datagram_offset
        constexpr size_t dg_ptr_off = T::header_size + T::ndp_datagram_offset;
        // DatagramIndex = 4 < header_size
        write_le<index_t>(buf.data() + dg_ptr_off, 4);
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("invalid NTB: datagram extends beyond BlockLength", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        using index_t = typename T::index_t;
        auto buf = make_ntb_ndp_at_front<BIT_SIZE>();
        constexpr size_t dg_ptr_off = T::header_size + T::ndp_datagram_offset;
        // DatagramLength follows DatagramIndex
        write_le<index_t>(buf.data() + dg_ptr_off + sizeof(index_t),
                          static_cast<index_t>(buf.size() + T::datagram_ptr_size));
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("invalid NTB: DatagramLength below minimum (14)", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        using index_t = typename T::index_t;
        auto buf = make_ntb_ndp_at_front<BIT_SIZE>();
        constexpr size_t dg_ptr_off = T::header_size + T::ndp_datagram_offset;
        // 8 < DG_MIN=14
        write_le<index_t>(buf.data() + dg_ptr_off + sizeof(index_t), 8);
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    TEMPLATE_TEST_CASE("invalid NTB: null terminator missing", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using T = ntb_traits<BIT_SIZE>;
        auto buf = make_ntb_ndp_at_front<BIT_SIZE>();
        // terminator (Datagram[1]) follows Datagram[0]
        constexpr size_t term_off = T::header_size + T::ndp_datagram_offset + T::datagram_ptr_size;
        buf[term_off] = 0x10; // non-zero DatagramIndex
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // --- Chained NDP signature check ----------------------------------------

    TEMPLATE_TEST_CASE("invalid NTB: second NDP has wrong signature", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_two_ndps<BIT_SIZE>();
        // Compute NDP1 offset, matching make_ntb_two_ndps<BIT_SIZE> layout
        constexpr size_t ndp0_off = ntb_traits<BIT_SIZE>::header_size + DG_SIZE;
        constexpr size_t ndp0_len =
            ntb_traits<BIT_SIZE>::ndp_datagram_offset + 2 * ntb_traits<BIT_SIZE>::datagram_ptr_size;
        constexpr size_t ndp1_off = c2usb::aligned_size<uint32_t>(ndp0_off + ndp0_len);
        buf[ndp1_off] = 'X';
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // An NTB with an NDP containing no datagrams has nothing to process,
    // so is_valid_ntb correctly rejects it.
    TEMPLATE_TEST_CASE("invalid NTB: NDP with no datagrams", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_empty_ndp<BIT_SIZE>();
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // Circular NextNdpIndex references must be rejected instead of causing an
    // unbounded traversal loop (DoS via crafted/corrupt NTB).
    TEMPLATE_TEST_CASE("invalid NTB: circular NDP chain does not cause infinite loop", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto buf = make_ntb_circular_ndps<BIT_SIZE>();
        CHECK(not usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(
            std::span<const uint8_t>(buf.data(), buf.size())));
    };

    // =======================================================================
    // ntb_rx_ctx - extract datagrams from builder-constructed NTBs
    // =======================================================================
    //
    // pop_datagram() returns spans whose base is this->data(page), so the
    // NTB bytes must reside in the rx_ctx buffer (use make_rx_ctx helper).

    TEMPLATE_TEST_CASE("ntb_rx_ctx: NDP at end, one datagram", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto ntb = make_ntb_ndp_at_end<BIT_SIZE>();
        auto [ctx, storage] = make_rx_ctx<BIT_SIZE>(ntb);

        // The datagram lives at header_size offset, length DG_SIZE, content all zeros
        auto dg = ctx.pop_datagram();
        CHECK(dg.size() == DG_SIZE);
        CHECK(std::ranges::all_of(dg, [](uint8_t b) { return b == 0; }));
        CHECK(ctx.pop_datagram().empty());
    };

    TEMPLATE_TEST_CASE("ntb_rx_ctx: NDP at front, one datagram", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto ntb = make_ntb_ndp_at_front<BIT_SIZE>();
        auto [ctx, storage] = make_rx_ctx<BIT_SIZE>(ntb);

        auto dg = ctx.pop_datagram();
        CHECK(dg.size() == DG_SIZE);
        CHECK(std::ranges::all_of(dg, [](uint8_t b) { return b == 0; }));
        CHECK(ctx.pop_datagram().empty());
    };

    TEMPLATE_TEST_CASE("ntb_rx_ctx: NDP in middle, two datagrams", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto ntb = make_ntb_ndp_in_middle<BIT_SIZE>();
        auto [ctx, storage] = make_rx_ctx<BIT_SIZE>(ntb);

        auto dg0 = ctx.pop_datagram();
        CHECK(dg0.size() == DG_SIZE);
        auto dg1 = ctx.pop_datagram();
        CHECK(dg1.size() == DG_SIZE);
        CHECK(ctx.pop_datagram().empty());
    };

    TEMPLATE_TEST_CASE("ntb_rx_ctx: two chained NDPs, one datagram each", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto ntb = make_ntb_two_ndps<BIT_SIZE>();
        auto [ctx, storage] = make_rx_ctx<BIT_SIZE>(ntb);

        auto dg0 = ctx.pop_datagram();
        CHECK(dg0.size() == DG_SIZE);
        auto dg1 = ctx.pop_datagram();
        CHECK(dg1.size() == DG_SIZE);
        CHECK(ctx.pop_datagram().empty());
    };

    // Verify that rx_ctx returns the exact bytes that were placed in the NTB.
    TEMPLATE_TEST_CASE("ntb_rx_ctx: datagram content is preserved", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        using idx_t = ntb_traits<BIT_SIZE>::index_t;
        // Build NTB manually with distinct datagram content
        idx_t dg_off = static_cast<idx_t>(ntb_traits<BIT_SIZE>::header_size);
        idx_t ndp_off = static_cast<idx_t>(dg_off + DG_SIZE);
        size_t ndp_len =
            ntb_traits<BIT_SIZE>::ndp_datagram_offset + 2 * ntb_traits<BIT_SIZE>::datagram_ptr_size;
        idx_t block_len = static_cast<idx_t>(c2usb::aligned_size<uint32_t>(ndp_off + ndp_len));

        std::vector<uint8_t> ntb(block_len, 0);
        ntb_traits<BIT_SIZE>::write_header(ntb.data(), 0, block_len, ndp_off);
        // Fill datagram with recognizable pattern
        for (size_t i = 0; i < DG_SIZE; ++i)
        {
            ntb[dg_off + i] = static_cast<uint8_t>(i + 1);
        }
        std::pair<idx_t, idx_t> dg{dg_off, static_cast<idx_t>(DG_SIZE)};
        ntb_traits<BIT_SIZE>::write_ndp(ntb.data(), ndp_off, 0,
                                        std::span<const std::pair<idx_t, idx_t>>(&dg, 1));

        auto [ctx, storage] = make_rx_ctx<BIT_SIZE>(ntb);
        auto popped = ctx.pop_datagram();
        CHECK(popped.size() == DG_SIZE);
        for (size_t i = 0; i < DG_SIZE; ++i)
        {
            CHECK(popped[i] == static_cast<uint8_t>(i + 1));
        }
        CHECK(ctx.pop_datagram().empty());
    };

    // =======================================================================
    // ntb_tx_ctx - construct NTBs and verify output
    // =======================================================================
    //
    // tx_ctx produces NDP-at-end layout: | header | datagrams | NDP |
    // BlockLength = NdpIndex + dpt->Length.

    TEMPLATE_TEST_CASE("ntb_tx_ctx: one datagram, output matches make_ntb_ndp_at_end", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        // make_ntb_ndp_at_end uses a zero-filled datagram of DG_SIZE bytes,
        // which is exactly what tx_ctx produces for a zero-filled append.
        auto expected = make_ntb_ndp_at_end<BIT_SIZE>();
        size_t page_bytes = expected.size();

        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(page_bytes);
        std::array<uint8_t, DG_SIZE> dg_data{};
        CHECK(ctx.append_datagram(dg_data.begin(), dg_data.end()));
        auto ntb = ctx.pop_ntb();

        CHECK(ntb.size() == expected.size());
        CHECK(std::ranges::equal(ntb, std::span<const uint8_t>(expected)));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: pop_ntb with no datagrams returns empty", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(256);
        auto ntb = ctx.pop_ntb();
        CHECK(ntb.empty());
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: datagram too small is rejected", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(256);
        std::array<uint8_t, DG_MIN - 1> small{};
        CHECK(not ctx.append_datagram(small.begin(), small.end()));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: datagram too large for remaining space is rejected", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        // Use a very small page: just enough for header + one DG_SIZE datagram + NDP
        auto expected = make_ntb_ndp_at_end<BIT_SIZE>();
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(expected.size());
        std::array<uint8_t, DG_SIZE> dg{};
        CHECK(ctx.append_datagram(dg.begin(), dg.end()));     // first fits
        CHECK(not ctx.append_datagram(dg.begin(), dg.end())); // second doesn't
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: sequence number increments across pop_ntb calls", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(256);
        std::array<uint8_t, DG_SIZE> dg{};

        ctx.append_datagram(dg.begin(), dg.end());
        auto ntb0 = ctx.pop_ntb();
        auto seq0 = static_cast<uint16_t>(ntb0[6]) | (static_cast<uint16_t>(ntb0[7]) << 8);

        ctx.append_datagram(dg.begin(), dg.end());
        auto ntb1 = ctx.pop_ntb();
        auto seq1 = static_cast<uint16_t>(ntb1[6]) | (static_cast<uint16_t>(ntb1[7]) << 8);

        CHECK(seq1 == static_cast<uint16_t>(seq0 + 1));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: output is a valid NTB", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(256);
        std::array<uint8_t, DG_SIZE> dg{};
        ctx.append_datagram(dg.begin(), dg.end());
        auto ntb = ctx.pop_ntb();
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(ntb));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: output with two datagrams is a valid NTB", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(512);
        std::array<uint8_t, DG_SIZE> dg{};
        ctx.append_datagram(dg.begin(), dg.end());
        ctx.append_datagram(dg.begin(), dg.end());
        auto ntb = ctx.pop_ntb();
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(ntb));
    };

    // --- allocate_datagram + commit_datagram ---------------------------------

    TEMPLATE_TEST_CASE("ntb_tx_ctx: allocate_datagram returns empty for too-small size", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(256);
        CHECK(ctx.allocate_datagram(DG_MIN - 1).empty());
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: allocate_datagram returns empty when no space", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto expected = make_ntb_ndp_at_end<BIT_SIZE>();
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(expected.size());
        auto span = ctx.allocate_datagram(DG_SIZE);
        CHECK(not span.empty());
        ctx.commit_datagram(span);
        CHECK(ctx.allocate_datagram(DG_SIZE).empty());
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: allocate_datagram returns empty when dg_max_limit hit", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(256);
        ctx.dg_max_limit = 1;
        auto span = ctx.allocate_datagram(DG_SIZE);
        CHECK(not span.empty());
        ctx.commit_datagram(span);
        CHECK(ctx.allocate_datagram(DG_SIZE).empty());
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: allocate+commit output matches make_ntb_ndp_at_end", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto expected = make_ntb_ndp_at_end<BIT_SIZE>();
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(expected.size());
        auto span = ctx.allocate_datagram(DG_SIZE);
        CHECK(not span.empty());
        std::fill(span.begin(), span.end(), uint8_t{0});
        ctx.commit_datagram(span);
        auto ntb = ctx.pop_ntb();
        CHECK(ntb.size() == expected.size());
        CHECK(std::ranges::equal(ntb, std::span<const uint8_t>(expected)));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: allocate+commit two datagrams is a valid NTB", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(512);
        auto sp0 = ctx.allocate_datagram(DG_SIZE);
        CHECK(not sp0.empty());
        ctx.commit_datagram(sp0);
        auto sp1 = ctx.allocate_datagram(DG_SIZE);
        CHECK(not sp1.empty());
        ctx.commit_datagram(sp1);
        auto ntb = ctx.pop_ntb();
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(ntb));
    };

    // =======================================================================
    // Round-trip: tx_ctx -> rx_ctx
    // =======================================================================
    //
    // Assemble an NTB with tx_ctx, feed the output into rx_ctx, verify all
    // popped datagrams equal the originally appended datagrams.

    TEMPLATE_TEST_CASE("round-trip: single datagram with known content", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        constexpr size_t page_size = 512;
        auto [tx, tx_storage] = make_tx_ctx<BIT_SIZE>(page_size);

        std::array<uint8_t, DG_SIZE> original{};
        for (size_t i = 0; i < DG_SIZE; ++i)
        {
            original[i] = static_cast<uint8_t>(0xA0 + i);
        }
        CHECK(tx.append_datagram(original.begin(), original.end()));
        auto ntb_span = tx.pop_ntb();

        // Copy into rx_ctx buffer
        std::vector<uint8_t> ntb_copy(ntb_span.begin(), ntb_span.end());
        auto [rx, rx_storage] = make_rx_ctx<BIT_SIZE>(ntb_copy);

        auto result = drain_rx(rx);
        CHECK(result.size() == 1u);
        CHECK(std::ranges::equal(result[0], original));
    };

    TEMPLATE_TEST_CASE("round-trip: two datagrams with distinct content", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        constexpr size_t page_size = 512;
        auto [tx, tx_storage] = make_tx_ctx<BIT_SIZE>(page_size);

        std::array<uint8_t, DG_SIZE> dg0{}, dg1{};
        for (size_t i = 0; i < DG_SIZE; ++i)
        {
            dg0[i] = static_cast<uint8_t>(0x11 + i);
            dg1[i] = static_cast<uint8_t>(0xAA - i);
        }
        CHECK(tx.append_datagram(dg0.begin(), dg0.end()));
        CHECK(tx.append_datagram(dg1.begin(), dg1.end()));
        auto ntb_span = tx.pop_ntb();

        std::vector<uint8_t> ntb_copy(ntb_span.begin(), ntb_span.end());
        auto [rx, rx_storage] = make_rx_ctx<BIT_SIZE>(ntb_copy);

        auto result = drain_rx(rx);
        CHECK(result.size() == 2u);
        CHECK(std::ranges::equal(result[0], dg0));
        CHECK(std::ranges::equal(result[1], dg1));
    };

    TEMPLATE_TEST_CASE("round-trip: consecutive pop_ntb calls produce independent NTBs", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        constexpr size_t page_size = 512;
        auto [tx, tx_storage] = make_tx_ctx<BIT_SIZE>(page_size);

        std::array<uint8_t, DG_SIZE> dg0{}, dg1{};
        for (size_t i = 0; i < DG_SIZE; ++i)
        {
            dg0[i] = static_cast<uint8_t>(0x01 + i);
            dg1[i] = static_cast<uint8_t>(0x81 + i);
        }

        // First NTB
        tx.append_datagram(dg0.begin(), dg0.end());
        auto span0 = tx.pop_ntb();
        std::vector<uint8_t> ntb0(span0.begin(), span0.end());

        // Second NTB
        tx.append_datagram(dg1.begin(), dg1.end());
        auto span1 = tx.pop_ntb();
        std::vector<uint8_t> ntb1(span1.begin(), span1.end());

        // Verify NTB0
        auto [rx0, rx0_storage] = make_rx_ctx<BIT_SIZE>(ntb0);
        auto result0 = drain_rx(rx0);
        CHECK(result0.size() == 1u);
        CHECK(std::ranges::equal(result0[0], dg0));

        // Verify NTB1
        auto [rx1, rx1_storage] = make_rx_ctx<BIT_SIZE>(ntb1);
        auto result1 = drain_rx(rx1);
        CHECK(result1.size() == 1u);
        CHECK(std::ranges::equal(result1[0], dg1));
    };

    TEMPLATE_TEST_CASE("round-trip: max datagrams limited by dg_max_limit", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        constexpr size_t page_size = 512;
        auto [tx, tx_storage] = make_tx_ctx<BIT_SIZE>(page_size);
        tx.dg_max_limit = 2;

        std::array<uint8_t, DG_SIZE> dg{};
        CHECK(tx.append_datagram(dg.begin(), dg.end()));
        CHECK(tx.append_datagram(dg.begin(), dg.end()));
        CHECK(not tx.append_datagram(dg.begin(), dg.end())); // limit hit

        auto ntb_span = tx.pop_ntb();
        std::vector<uint8_t> ntb_copy(ntb_span.begin(), ntb_span.end());
        auto [rx, rx_storage] = make_rx_ctx<BIT_SIZE>(ntb_copy);
        auto result = drain_rx(rx);
        CHECK(result.size() == 2u);
    };

    TEMPLATE_TEST_CASE("round-trip: single datagram via allocate+commit", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        constexpr size_t page_size = 512;
        auto [tx, tx_storage] = make_tx_ctx<BIT_SIZE>(page_size);

        std::array<uint8_t, DG_SIZE> original{};
        for (size_t i = 0; i < DG_SIZE; ++i)
        {
            original[i] = static_cast<uint8_t>(0xA0 + i);
        }

        auto span = tx.allocate_datagram(DG_SIZE);
        CHECK(not span.empty());
        std::copy(original.begin(), original.end(), span.begin());
        tx.commit_datagram(span);
        auto ntb_span = tx.pop_ntb();

        std::vector<uint8_t> ntb_copy(ntb_span.begin(), ntb_span.end());
        auto [rx, rx_storage] = make_rx_ctx<BIT_SIZE>(ntb_copy);
        auto result = drain_rx(rx);
        CHECK(result.size() == 1u);
        CHECK(std::ranges::equal(result[0], original));
    };

    TEMPLATE_TEST_CASE("round-trip: two datagrams via allocate+commit with distinct content", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        constexpr size_t page_size = 512;
        auto [tx, tx_storage] = make_tx_ctx<BIT_SIZE>(page_size);

        std::array<uint8_t, DG_SIZE> dg0{}, dg1{};
        for (size_t i = 0; i < DG_SIZE; ++i)
        {
            dg0[i] = static_cast<uint8_t>(0x11 + i);
            dg1[i] = static_cast<uint8_t>(0xAA - i);
        }

        auto sp0 = tx.allocate_datagram(DG_SIZE);
        CHECK(not sp0.empty());
        std::copy(dg0.begin(), dg0.end(), sp0.begin());
        tx.commit_datagram(sp0);

        auto sp1 = tx.allocate_datagram(DG_SIZE);
        CHECK(not sp1.empty());
        std::copy(dg1.begin(), dg1.end(), sp1.begin());
        tx.commit_datagram(sp1);

        auto ntb_span = tx.pop_ntb();
        std::vector<uint8_t> ntb_copy(ntb_span.begin(), ntb_span.end());
        auto [rx, rx_storage] = make_rx_ctx<BIT_SIZE>(ntb_copy);
        auto result = drain_rx(rx);
        CHECK(result.size() == 2u);
        CHECK(std::ranges::equal(result[0], dg0));
        CHECK(std::ranges::equal(result[1], dg1));
    };

    // =======================================================================
    // ntb_tx_ctx - armed state tracking
    // =======================================================================
    //
    // The armed NTB is the one on the page opposite to the one being populated,
    // its non-zero BlockLength marks it armed.

    TEMPLATE_TEST_CASE("ntb_tx_ctx: fresh context is not armed", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        CHECK(not ctx.armed());
        CHECK(ctx.armed_ntb().empty());
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: arm without datagrams does nothing", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        CHECK(not ctx.arm());
        CHECK(not ctx.armed());
        CHECK(ctx.armed_ntb().empty());
        // the empty arm attempt must not have consumed the page or sequence
        CHECK(append_filled(ctx, 0x5A));
        CHECK(ctx.arm());
        CHECK(ntb_sequence(ctx.armed_ntb()) == 0);
        CHECK(ctx.armed_ntb().data() == tx_page(*storage, 0));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: arm one datagram, armed NTB matches make_ntb_ndp_at_end", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto expected = make_ntb_ndp_at_end<BIT_SIZE>();
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(expected.size());
        std::array<uint8_t, DG_SIZE> dg{};
        CHECK(ctx.append_datagram(dg.begin(), dg.end()));
        CHECK(ctx.arm());
        CHECK(ctx.armed());
        auto ntb = ctx.armed_ntb();
        CHECK(ntb.size() == expected.size());
        CHECK(std::ranges::equal(ntb, std::span<const uint8_t>(expected)));
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(ntb));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: arm while armed keeps the armed NTB intact", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        CHECK(append_filled(ctx, 0x11));
        CHECK(ctx.arm());
        auto first = ctx.armed_ntb();
        std::vector<uint8_t> first_copy(first.begin(), first.end());

        // datagrams keep being assembled on the other page while one is armed
        CHECK(append_filled(ctx, 0x22));
        CHECK(append_filled(ctx, 0x33));
        CHECK(ctx.arm());
        auto still = ctx.armed_ntb();
        CHECK(still.data() == first.data());
        CHECK(std::ranges::equal(still, first_copy));

        // the armed NTB still decodes to only the first datagram
        auto dgs = rx_roundtrip<BIT_SIZE>(still);
        CHECK(dgs.size() == 1u);
        CHECK(std::ranges::all_of(dgs[0], [](uint8_t b) { return b == 0x11; }));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: disarm releases the armed NTB", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        CHECK(append_filled(ctx, 0x11));
        CHECK(ctx.arm());
        ctx.disarm();
        CHECK(not ctx.armed());
        CHECK(ctx.armed_ntb().empty());
        // disarming twice is harmless
        ctx.disarm();
        CHECK(not ctx.armed());
        // nothing left to send
        CHECK(not ctx.arm());
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: datagrams assembled while armed are sent next", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        CHECK(append_filled(ctx, 0x11));
        CHECK(ctx.arm());
        auto seq0 = ntb_sequence(ctx.armed_ntb());
        CHECK(append_filled(ctx, 0x22));
        CHECK(append_filled(ctx, 0x33));

        ctx.disarm();
        CHECK(ctx.arm());
        auto ntb = ctx.armed_ntb();
        CHECK(ntb.data() == tx_page(*storage, 1));
        CHECK(ntb_sequence(ntb) == static_cast<uint16_t>(seq0 + 1));
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(ntb));

        auto dgs = rx_roundtrip<BIT_SIZE>(ntb);
        CHECK(dgs.size() == 2u);
        CHECK(std::ranges::all_of(dgs[0], [](uint8_t b) { return b == 0x22; }));
        CHECK(std::ranges::all_of(dgs[1], [](uint8_t b) { return b == 0x33; }));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: arm/disarm cycles alternate pages and increment sequence",
                       ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        for (uint8_t i = 0; i < 6; ++i)
        {
            auto fill = static_cast<uint8_t>(0x40 + i);
            CHECK(append_filled(ctx, fill));
            CHECK(ctx.arm());
            auto ntb = ctx.armed_ntb();
            CHECK(ntb.data() == tx_page(*storage, i % 2));
            CHECK(ntb_sequence(ntb) == i);

            auto dgs = rx_roundtrip<BIT_SIZE>(ntb);
            CHECK(dgs.size() == 1u);
            CHECK(std::ranges::all_of(dgs[0], [fill](uint8_t b) { return b == fill; }));
            ctx.disarm();
            CHECK(not ctx.armed());
        }
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: reset clears stale armed state from a dirty buffer", ntb16,
                       ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        // emulate stale buffer contents, e.g. from a previous session
        std::ranges::fill(*storage, 0xA5A5A5A5u);
        ctx.page = 1;
        ctx.reset();
        CHECK(ctx.page == 0);
        CHECK(not ctx.armed());
        CHECK(ctx.armed_ntb().empty());

        CHECK(append_filled(ctx, 0x77));
        CHECK(ctx.arm());
        auto ntb = ctx.armed_ntb();
        CHECK(ntb.data() == tx_page(*storage, 0));
        CHECK(ntb_sequence(ntb) == 0);
        CHECK(usb::cdc::ncm::is_valid_ntb<BIT_SIZE>(ntb));
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: reset discards armed and pending datagrams", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        CHECK(append_filled(ctx, 0x11));
        CHECK(ctx.arm());
        CHECK(append_filled(ctx, 0x22));
        ctx.reset();
        CHECK(not ctx.armed());
        CHECK(not ctx.arm());

        // sequence numbering restarts
        CHECK(append_filled(ctx, 0x33));
        CHECK(ctx.arm());
        CHECK(ntb_sequence(ctx.armed_ntb()) == 0);
    };

    TEMPLATE_TEST_CASE("ntb_tx_ctx: pop_ntb releases the armed NTB and arms the next", ntb16, ntb32)
    {
        constexpr size_t BIT_SIZE = TestType::value;
        auto [ctx, storage] = make_tx_ctx<BIT_SIZE>(TX_PAGE_SIZE);
        CHECK(append_filled(ctx, 0x11));
        CHECK(ctx.arm());
        CHECK(append_filled(ctx, 0x22));

        auto ntb = ctx.pop_ntb();
        CHECK(ctx.armed());
        CHECK(ntb.data() == ctx.armed_ntb().data());
        CHECK(ntb.data() == tx_page(*storage, 1));
        auto dgs = rx_roundtrip<BIT_SIZE>(ntb);
        CHECK(dgs.size() == 1u);
        CHECK(std::ranges::all_of(dgs[0], [](uint8_t b) { return b == 0x22; }));

        // nothing pending: the previous NTB is released and nothing is armed
        CHECK(ctx.pop_ntb().empty());
        CHECK(not ctx.armed());
    };
};
