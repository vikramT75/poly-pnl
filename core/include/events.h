#pragma once

#include <cstdint>
#include <cstddef>

// ============================================================================
// Normalized event structs for the poly-pnl engine.
//
// Design constraints:
//   - Fixed size: enables cache-friendly arrays and zero-copy IPC transport
//   - No pointers / no heap: safe to mmap and share across processes
//   - static_assert on every size/alignment: catches ABI drift immediately
//   - All monetary values: int64_t, scale 1e-6 (1 unit = $0.000001 USDC)
//   - All quantities:      int64_t, scale 1e-6 (1 unit = 0.000001 shares)
//   - Wallet IDs:          uint32_t interned index (20-byte addr → dense uint32)
//   - Token IDs:           uint32_t interned index (256-bit token_id → dense uint32)
// ============================================================================

namespace polypnl {

// ---------------------------------------------------------------------------
// Interned ID types (opaque integers; meaning comes from the intern tables)
// ---------------------------------------------------------------------------
using WalletIdx = uint32_t;
using TokenIdx  = uint32_t;

static constexpr WalletIdx INVALID_WALLET = 0xFFFFFFFFu;
static constexpr TokenIdx  INVALID_TOKEN  = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// EventKind — discriminates the union-of-event-data in NormalizedEvent
// ---------------------------------------------------------------------------
enum class EventKind : uint8_t {
    FILL            = 0,  // Buy or sell fill from CTF Exchange / NegRisk Exchange
    SPLIT           = 1,  // PositionSplit: collateral → YES + NO tokens
    MERGE           = 2,  // PositionsMerge: YES + NO tokens → collateral
    REDEEM          = 3,  // PayoutRedemption: outcome token → collateral at payout
    TRANSFER_IN     = 4,  // ERC-1155 TransferSingle/Batch received (non-trade)
    TRANSFER_OUT    = 5,  // ERC-1155 TransferSingle/Batch sent (non-trade)
    MARK_UPDATE     = 6,  // CLOB price update (not on-chain; injected by ingestor)
    RESOLUTION      = 7,  // Market resolved (synthetic; from Gamma API polling)
    ROLLBACK_BEGIN  = 8,  // Begin rollback of events from a reorged block
    ROLLBACK_END    = 9,  // End of rollback; next events are canonical replacements
    METADATA_UPDATE = 10, // Market metadata update (token ↔ condition ↔ outcome)
};

// ---------------------------------------------------------------------------
// FillSide — direction of a FILL event
// ---------------------------------------------------------------------------
enum class FillSide : uint8_t {
    BUY  = 0,  // Wallet bought outcome tokens (paid USDC, received tokens)
    SELL = 1,  // Wallet sold outcome tokens (received USDC, gave tokens)
};

// ---------------------------------------------------------------------------
// NormalizedEvent — the single event type flowing through the pipeline.
//
// Size target: 64 bytes (one cache line). Verified by static_assert below.
//
// Field layout (intentionally packed to avoid padding waste):
//   offset  0: block_number    (8 bytes)
//   offset  8: log_index       (4 bytes)
//   offset 12: tx_index        (4 bytes)
//   offset 16: timestamp_s     (8 bytes) — Unix seconds of block
//   offset 24: wallet_idx      (4 bytes)
//   offset 28: token_idx       (4 bytes)
//   offset 32: qty             (8 bytes) — in 1e-6 shares
//   offset 40: price           (8 bytes) — in 1e-6 USDC/share
//   offset 48: fee             (4 bytes) — in 1e-6 USDC (fits in int32 for typical fees)
//   offset 52: kind            (1 byte)
//   offset 53: side            (1 byte)  — only meaningful for FILL
//   offset 54: flags           (1 byte)  — bitmask, see EventFlags
//   offset 55: _pad            (1 byte)
//   offset 56: aux_token_idx   (4 bytes) — SPLIT/MERGE: the complementary token
//   offset 60: payout_price    (4 bytes) — REDEEM/RESOLUTION: int32 (0 or 1_000_000)
// Total: 64 bytes
// ---------------------------------------------------------------------------

struct EventFlags {
    static constexpr uint8_t SYNTHETIC        = 0x01; // Not an on-chain event
    static constexpr uint8_t INCOMPLETE_HIST  = 0x02; // Position has truncated history
    static constexpr uint8_t HAS_TRANSFERS    = 0x04; // ERC-1155 transfer affected this
    static constexpr uint8_t NEGRISK          = 0x08; // NegRisk contract event
    static constexpr uint8_t MAKER            = 0x10; // This wallet is the maker (not taker)
};

struct NormalizedEvent {
    uint64_t   block_number;    //  0: Polygon block number
    uint32_t   log_index;       //  8: Index of this log within the block
    uint32_t   tx_index;        // 12: Index of the transaction within the block
    int64_t    timestamp_s;     // 16: Block timestamp (Unix seconds)
    WalletIdx  wallet_idx;      // 24: Interned wallet index
    TokenIdx   token_idx;       // 28: Interned outcome token index
    int64_t    qty;             // 32: Quantity in 1e-6 shares (always positive)
    int64_t    price;           // 40: Price in 1e-6 USDC/share (0 if not applicable)
    int32_t    fee;             // 48: Fee in 1e-6 USDC (taker fee; 0 for maker)
    EventKind  kind;            // 52: Event discriminator
    FillSide   side;            // 53: BUY or SELL (FILL only)
    uint8_t    flags;           // 54: Bitmask of EventFlags
    uint8_t    _pad0;           // 55: Reserved (must be zero)
    TokenIdx   aux_token_idx;   // 56: Complementary token (SPLIT/MERGE) or INVALID_TOKEN
    int32_t    payout_price;    // 60: Payout in 1e-6 USDC (REDEEM/RESOLUTION); 0 or 1_000_000
};

static_assert(sizeof(NormalizedEvent) == 64,
    "NormalizedEvent must be exactly 64 bytes (one cache line)");
static_assert(alignof(NormalizedEvent) == 8,
    "NormalizedEvent must be 8-byte aligned");
static_assert(__builtin_offsetof(NormalizedEvent, block_number)  ==  0);
static_assert(__builtin_offsetof(NormalizedEvent, log_index)     ==  8);
static_assert(__builtin_offsetof(NormalizedEvent, tx_index)      == 12);
static_assert(__builtin_offsetof(NormalizedEvent, timestamp_s)   == 16);
static_assert(__builtin_offsetof(NormalizedEvent, wallet_idx)    == 24);
static_assert(__builtin_offsetof(NormalizedEvent, token_idx)     == 28);
static_assert(__builtin_offsetof(NormalizedEvent, qty)           == 32);
static_assert(__builtin_offsetof(NormalizedEvent, price)         == 40);
static_assert(__builtin_offsetof(NormalizedEvent, fee)           == 48);
static_assert(__builtin_offsetof(NormalizedEvent, kind)          == 52);
static_assert(__builtin_offsetof(NormalizedEvent, side)          == 53);
static_assert(__builtin_offsetof(NormalizedEvent, flags)         == 54);
static_assert(__builtin_offsetof(NormalizedEvent, aux_token_idx) == 56);
static_assert(__builtin_offsetof(NormalizedEvent, payout_price)  == 60);

// ---------------------------------------------------------------------------
// RawLog — the raw on-chain log as delivered by the ingestor over the socket.
// The decoder converts RawLog → NormalizedEvent.
//
// All byte arrays are ABI-encoded log data exactly as returned by the RPC.
// ---------------------------------------------------------------------------

static constexpr size_t MAX_TOPICS = 4;
static constexpr size_t MAX_DATA_BYTES = 1024; // generous; actual CTF events are ~256 bytes

struct RawLog {
    uint64_t block_number;
    int64_t  timestamp_s;
    uint32_t log_index;
    uint32_t tx_index;
    uint8_t  address[20];           // Contract address (20 bytes, no checksum)
    uint8_t  topics[MAX_TOPICS][32]; // Up to 4 topics, each 32 bytes
    uint8_t  num_topics;
    uint8_t  _pad[3];
    uint32_t data_len;              // Actual length of data[]
    uint8_t  data[MAX_DATA_BYTES];  // Non-indexed log data (ABI-encoded)
    uint8_t  _tail_pad[4];          // Align total size to multiple of 8
};

static_assert(sizeof(RawLog) == 1208,
    "RawLog size check");

// ---------------------------------------------------------------------------
// MarkUpdate — live CLOB price, injected as a synthetic event
// (also representable as a NormalizedEvent with kind=MARK_UPDATE)
// ---------------------------------------------------------------------------
struct MarkUpdate {
    TokenIdx token_idx;
    uint8_t  _pad[4];
    int64_t  mark_price; // in 1e-6 USDC/share
    int64_t  timestamp_ms; // milliseconds since epoch
};

static_assert(sizeof(MarkUpdate) == 24);

// ---------------------------------------------------------------------------
// Position — per-(wallet, token) state maintained by the PnL engine
// ---------------------------------------------------------------------------
struct Position {
    int64_t  size;               // in 1e-6 shares (0 if fully sold/redeemed)
    int64_t  avg_price;          // in 1e-6 USDC/share (0 if size == 0)
    int64_t  realized_pnl;       // in 1e-6 USDC (cumulative, fees deducted)
    int64_t  running_unrealized; // cached: size * (mark - avg_price) / 1e6
    uint32_t market_idx;         // Interned market index (for grouping)
    bool     incomplete_history; // true if window start truncated buys
    bool     has_transfers;      // true if ERC-1155 transfers affected this
    bool     has_negrisk;        // true if NegRisk conversions affected this
    bool     is_resolved;        // true if market resolved and synthetic REDEEM applied
    bool     is_synthetic;       // true if synthetic event
    uint8_t  _pad[3];
};

static_assert(sizeof(Position) == 48, "Position must be 48 bytes");
static_assert(alignof(Position) == 8);

} // namespace polypnl
