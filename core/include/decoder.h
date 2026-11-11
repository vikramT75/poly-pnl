#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "events.h"

// ============================================================================
// ABI Decoder — converts raw Polygon log bytes into NormalizedEvents.
//
// Design:
//   - Hand-written ABI decoding for each event type (no generic ABI library)
//   - Zero-copy parsing: operates directly on the RawLog byte arrays
//   - No exceptions, no RTTI, no heap allocation
//   - All known event topic0 hashes are compile-time constants (VERIFIED)
//   - Fuzzer-safe: bounds-checked every read; returns 0 on malformed input
//
// V1/V2 awareness:
//   The CTF Exchange upgraded at ~block 86,126,998 (April 28, 2026).
//   V1 OrderFilled: 8 params (dual asset IDs)
//   V2 OrderFilled: 10 params (single tokenId + explicit side + builder/metadata)
//   Both are decoded correctly; version is determined by contract address.
//
// Source: docs/CONTRACTS_AND_APIS.md (Phase 0 verified)
// ============================================================================

namespace polypnl {
namespace decoder {

// ---------------------------------------------------------------------------
// Verified contract addresses (Polygon mainnet, 20 bytes, lowercase)
// Source: docs/CONTRACTS_AND_APIS.md
// ---------------------------------------------------------------------------

// V1 Exchange Addresses (pre block ~86,126,998)
static constexpr uint8_t CTF_EXCHANGE_V1[20] = {
    0x4b, 0xFb, 0x41, 0xd5, 0xB3, 0x57, 0x0D, 0xeF, 0xd0, 0x3C,
    0x39, 0xa9, 0xA4, 0xD8, 0xdE, 0x6B, 0xd8, 0xB8, 0x98, 0x2E
};
static constexpr uint8_t NEGRISK_EXCHANGE_V1[20] = {
    0xC5, 0xd5, 0x63, 0xA3, 0x6A, 0xE7, 0x81, 0x45, 0xC4, 0x5a,
    0x50, 0x13, 0x4d, 0x48, 0xA1, 0x21, 0x52, 0x20, 0xf8, 0x0a
};

// V2 Exchange Addresses (post block ~86,126,998)
static constexpr uint8_t CTF_EXCHANGE_V2[20] = {
    0xE1, 0x11, 0x18, 0x00, 0x00, 0xd2, 0x66, 0x3C, 0x00, 0x91,
    0xe4, 0xf4, 0x00, 0x23, 0x75, 0x45, 0xB8, 0x7B, 0x99, 0x6B
};
static constexpr uint8_t NEGRISK_EXCHANGE_V2[20] = {
    0xe2, 0x22, 0x2d, 0x27, 0x9d, 0x74, 0x40, 0x50, 0xd2, 0x8e,
    0x00, 0x52, 0x00, 0x10, 0x52, 0x00, 0x00, 0x31, 0x0F, 0x59
};

// CTF (Gnosis Conditional Tokens Framework) — same for V1 and V2
static constexpr uint8_t CTF_CONTRACT[20] = {
    0x4D, 0x97, 0xDC, 0xd9, 0x7e, 0xC9, 0x45, 0xf4, 0x0c, 0xF6,
    0x5F, 0x87, 0x09, 0x7A, 0xCe, 0x5E, 0xA0, 0x47, 0x60, 0x45
};

// NegRisk Adapter — same for V1 and V2
static constexpr uint8_t NEGRISK_ADAPTER[20] = {
    0xd9, 0x1E, 0x80, 0xcF, 0x2E, 0x7b, 0xe2, 0xe1, 0x62, 0xc6,
    0x51, 0x3c, 0xeD, 0x06, 0xf1, 0xdD, 0x0d, 0xa3, 0x52, 0x96
};

// Block number at which V2 exchange contracts became active
static constexpr uint64_t V2_UPGRADE_BLOCK = 86'126'998ULL;

// ---------------------------------------------------------------------------
// topic0 keccak256 hashes (VERIFIED — source: docs/CONTRACTS_AND_APIS.md)
// ---------------------------------------------------------------------------

// OrderFilled V1: OrderFilled(bytes32,address,address,uint256,uint256,uint256,uint256,uint256)
// keccak256 = 0xd0a08e8c493f9c94f29311604c9de1b4e8c8d4c06bd0c789af57f2d65bfec0f6
static constexpr uint8_t TOPIC0_ORDER_FILLED_V1[32] = {
    0xd0, 0xa0, 0x8e, 0x8c, 0x49, 0x3f, 0x9c, 0x94, 0xf2, 0x93, 0x11, 0x60,
    0x4c, 0x9d, 0xe1, 0xb4, 0xe8, 0xc8, 0xd4, 0xc0, 0x6b, 0xd0, 0xc7, 0x89,
    0xaf, 0x57, 0xf2, 0xd6, 0x5b, 0xfe, 0xc0, 0xf6
};

// OrderFilled V2: OrderFilled(bytes32,address,address,uint8,uint256,uint256,uint256,uint256,bytes32,bytes32)
// keccak256 = 0xd543adfd945773f1a62f74f0ee55a5e3b9b1a28262980ba90b1a89f2ea84d8ee
static constexpr uint8_t TOPIC0_ORDER_FILLED_V2[32] = {
    0xd5, 0x43, 0xad, 0xfd, 0x94, 0x57, 0x73, 0xf1, 0xa6, 0x2f, 0x74, 0xf0,
    0xee, 0x55, 0xa5, 0xe3, 0xb9, 0xb1, 0xa2, 0x82, 0x62, 0x98, 0x0b, 0xa9,
    0x0b, 0x1a, 0x89, 0xf2, 0xea, 0x84, 0xd8, 0xee
};

// PositionSplit(address,address,bytes32,bytes32,uint256[],uint256)
// keccak256 = 0x633dad6e82813589eb1765cd6c174f8841daea2913e2bb7a875880fcd07fb5e5
static constexpr uint8_t TOPIC0_POSITION_SPLIT[32] = {
    0x63, 0x3d, 0xad, 0x6e, 0x82, 0x81, 0x35, 0x89, 0xeb, 0x17, 0x65, 0xcd,
    0x6c, 0x17, 0x4f, 0x88, 0x41, 0xda, 0xea, 0x29, 0x13, 0xe2, 0xbb, 0x7a,
    0x87, 0x58, 0x80, 0xfc, 0xd0, 0x7f, 0xb5, 0xe5
};

// PositionsMerge(address,address,bytes32,bytes32,uint256[],uint256)
// keccak256 = 0x6f13ca62553fcc2bcd2372180a43949c1e4cebba603901ede2f4e14f36b282ca
static constexpr uint8_t TOPIC0_POSITIONS_MERGE[32] = {
    0x6f, 0x13, 0xca, 0x62, 0x55, 0x3f, 0xcc, 0x2b, 0xcd, 0x23, 0x72, 0x18,
    0x0a, 0x43, 0x94, 0x9c, 0x1e, 0x4c, 0xeb, 0xba, 0x60, 0x39, 0x01, 0xed,
    0xe2, 0xf4, 0xe1, 0x4f, 0x36, 0xb2, 0x82, 0xca
};

// PayoutRedemption(address,address,bytes32,bytes32,uint256[],uint256)
// keccak256 = 0x2682012a4a4f1973119f1c9b90745d1bd91fa2bab387344f044cb3586864d18d
static constexpr uint8_t TOPIC0_PAYOUT_REDEMPTION[32] = {
    0x26, 0x82, 0x01, 0x2a, 0x4a, 0x4f, 0x19, 0x73, 0x11, 0x9f, 0x1c, 0x9b,
    0x90, 0x74, 0x5d, 0x1b, 0xd9, 0x1f, 0xa2, 0xba, 0xb3, 0x87, 0x34, 0x4f,
    0x04, 0x4c, 0xb3, 0x58, 0x68, 0x64, 0xd1, 0x8d
};

// TransferSingle(address,address,address,uint256,uint256)
// keccak256 = 0xc3d58168c5ae7397731d063d5bbf3d657854427343f4c083240f7aacaa2d0f62
static constexpr uint8_t TOPIC0_TRANSFER_SINGLE[32] = {
    0xc3, 0xd5, 0x81, 0x68, 0xc5, 0xae, 0x73, 0x97, 0x73, 0x1d, 0x06, 0x3d,
    0x5b, 0xbf, 0x3d, 0x65, 0x78, 0x54, 0x42, 0x73, 0x43, 0xf4, 0xc0, 0x83,
    0x24, 0x0f, 0x7a, 0xac, 0xaa, 0x2d, 0x0f, 0x62
};

// TransferBatch(address,address,address,uint256[],uint256[])
// keccak256 = 0x4a39dc06d4c0dbc64b70af90fd698a233a518aa5d07e595d983b8c0526c8f7fb
static constexpr uint8_t TOPIC0_TRANSFER_BATCH[32] = {
    0x4a, 0x39, 0xdc, 0x06, 0xd4, 0xc0, 0xdb, 0xc6, 0x4b, 0x70, 0xaf, 0x90,
    0xfd, 0x69, 0x8a, 0x23, 0x3a, 0x51, 0x8a, 0xa5, 0xd0, 0x7e, 0x59, 0x5d,
    0x98, 0x3b, 0x8c, 0x05, 0x26, 0xc8, 0xf7, 0xfb
};

// PositionsConverted(address,bytes32,uint256,uint256) — NegRisk Adapter
// keccak256 = 0xb03d19dddbc72a87e735ff0ea3b57bef133ebe44e1894284916a84044deb367e
static constexpr uint8_t TOPIC0_POSITIONS_CONVERTED[32] = {
    0xb0, 0x3d, 0x19, 0xdd, 0xdb, 0xc7, 0x2a, 0x87, 0xe7, 0x35, 0xff, 0x0e,
    0xa3, 0xb5, 0x7b, 0xef, 0x13, 0x3e, 0xbe, 0x44, 0xe1, 0x89, 0x42, 0x84,
    0x91, 0x6a, 0x84, 0x04, 0x4d, 0xeb, 0x36, 0x7e
};

// ---------------------------------------------------------------------------
// ABI Decoding Helpers — bounds-checked, no exceptions, no allocation
// ---------------------------------------------------------------------------

// Read big-endian uint256 from 32 bytes at offset; return low 8 bytes as int64.
// Returns -1 if bounds check fails (caller must check).
[[nodiscard]] inline int64_t abi_read_i64(
    const uint8_t* data, uint32_t data_len, uint32_t offset) noexcept
{
    if (offset + 32 > data_len) return -1;
    uint64_t result = 0;
    for (int i = 24; i < 32; ++i)
        result = (result << 8) | data[offset + i];
    return static_cast<int64_t>(result);
}

// Read big-endian uint32 from the low 4 bytes of a 32-byte ABI slot.
[[nodiscard]] inline uint32_t abi_read_u32(
    const uint8_t* data, uint32_t data_len, uint32_t offset) noexcept
{
    if (offset + 32 > data_len) return 0xFFFFFFFF;
    uint32_t result = 0;
    for (int i = 28; i < 32; ++i)
        result = (result << 8) | data[offset + i];
    return result;
}

// Read a 20-byte address from ABI-encoded slot (left-padded to 32 bytes).
[[nodiscard]] inline bool abi_read_addr(
    const uint8_t* data, uint32_t data_len, uint32_t offset, uint8_t out[20]) noexcept
{
    if (offset + 32 > data_len) return false;
    std::memcpy(out, data + offset + 12, 20);
    return true;
}

// Read a full 32-byte slot (for token IDs, condition IDs, etc.)
[[nodiscard]] inline bool abi_read_b32(
    const uint8_t* data, uint32_t data_len, uint32_t offset, uint8_t out[32]) noexcept
{
    if (offset + 32 > data_len) return false;
    std::memcpy(out, data + offset, 32);
    return true;
}

// Compare 32 bytes (topic equality check)
[[nodiscard]] inline bool topic_eq(const uint8_t* a, const uint8_t* b) noexcept {
    return std::memcmp(a, b, 32) == 0;
}

// Compare 20 bytes (address equality check)
[[nodiscard]] inline bool addr_eq(const uint8_t* a, const uint8_t* b) noexcept {
    return std::memcmp(a, b, 20) == 0;
}

// Zero address check (minting/burning detection)
[[nodiscard]] inline bool addr_is_zero(const uint8_t addr[20]) noexcept {
    for (int i = 0; i < 20; ++i) if (addr[i]) return false;
    return true;
}

// ---------------------------------------------------------------------------
// InternTables — extern interface; actual tables live in the engine.
// ---------------------------------------------------------------------------
struct InternTables {
    WalletIdx (*intern_wallet)(const uint8_t addr[20], void* ctx) noexcept;
    TokenIdx  (*intern_token)(const uint8_t token_id[32], void* ctx) noexcept;
    void*     ctx;
};

// ---------------------------------------------------------------------------
// Decoder — stateless per-call; thread-safe if intern callbacks are thread-safe
// ---------------------------------------------------------------------------
class Decoder {
public:
    explicit Decoder(InternTables tables) noexcept : tables_(tables) {}

    // Decode a RawLog → NormalizedEvent(s).
    // Returns count of events written to out[0..n-1].
    // Returns 0 for unrecognized or malformed logs.
    // out_capacity must be >= MAX_EVENTS_PER_LOG (16).
    // Never throws; never allocates.
    static constexpr int MAX_EVENTS_PER_LOG = 16; // TransferBatch can produce many
    [[nodiscard]] int decode(const RawLog& log,
                             NormalizedEvent* out,
                             int out_capacity) const noexcept;

private:
    InternTables tables_;

    [[nodiscard]] int decode_order_filled_v1(const RawLog& log, NormalizedEvent* out, int out_cap, bool negrisk) const noexcept;
    [[nodiscard]] int decode_order_filled_v2(const RawLog& log, NormalizedEvent* out, int out_cap, bool negrisk) const noexcept;
    [[nodiscard]] int decode_position_split(const RawLog& log, NormalizedEvent* out) const noexcept;
    [[nodiscard]] int decode_positions_merge(const RawLog& log, NormalizedEvent* out) const noexcept;
    [[nodiscard]] int decode_payout_redemption(const RawLog& log, NormalizedEvent* out) const noexcept;
    [[nodiscard]] int decode_transfer_single(const RawLog& log, NormalizedEvent* out) const noexcept;
    [[nodiscard]] int decode_transfer_batch(const RawLog& log, NormalizedEvent* out, int out_cap) const noexcept;
    [[nodiscard]] int decode_positions_converted(const RawLog& log, NormalizedEvent* out) const noexcept;

    void init_event(NormalizedEvent& ev, const RawLog& log, EventKind kind) const noexcept {
        ev.block_number  = log.block_number;
        ev.log_index     = log.log_index;
        ev.tx_index      = log.tx_index;
        ev.timestamp_s   = log.timestamp_s;
        ev.kind          = kind;
        ev.side          = FillSide::BUY;
        ev.flags         = 0;
        ev._pad0         = 0;
        ev.qty           = 0;
        ev.price         = 0;
        ev.fee           = 0;
        ev.aux_token_idx = INVALID_TOKEN;
        ev.payout_price  = 0;
        ev.wallet_idx    = INVALID_WALLET;
        ev.token_idx     = INVALID_TOKEN;
    }

    WalletIdx intern_wallet(const uint8_t addr[20]) const noexcept {
        if (!tables_.intern_wallet) return INVALID_WALLET;
        return tables_.intern_wallet(addr, tables_.ctx);
    }

    TokenIdx intern_token(const uint8_t token_id[32]) const noexcept {
        if (!tables_.intern_token) return INVALID_TOKEN;
        return tables_.intern_token(token_id, tables_.ctx);
    }
};

} // namespace decoder
} // namespace polypnl
