#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <functional>
#include <optional>
#include <span>

#include "events.h"
#include "intern_table.h"

// ============================================================================
// PnL Engine — single-threaded v1 (Phase 3).
//
// Maintains per-(wallet, token) positions with average cost basis accounting.
// Processes NormalizedEvents in strict (block_number, log_index) order.
//
// Hot-path design:
//   - Flat open-addressing hash map for position lookup
//   - No heap allocation on the event processing path
//   - Arena allocator for position structs
//   - 128-block rollback journal for reorg support
//   - Deterministic: same event stream → byte-identical state
//
// See docs/ACCOUNTING_MODEL.md for formula details.
// See docs/EDGE_CASES.md for all edge-case policies.
// ============================================================================

namespace polypnl {
namespace engine {

// ---------------------------------------------------------------------------
// Leaderboard entry
// ---------------------------------------------------------------------------
struct LeaderboardEntry {
    WalletIdx wallet_idx;
    int64_t   total_pnl;      // realized + unrealized (1e-6 USDC)
    int64_t   realized_pnl;
    int64_t   unrealized_pnl;
    int64_t   volume;         // total collateral traded (1e-6 USDC)
    uint32_t  winning_markets;
    uint32_t  total_markets;
};

// ---------------------------------------------------------------------------
// WalletSummary — returned by API queries
// ---------------------------------------------------------------------------
struct WalletSummary {
    WalletIdx wallet_idx;
    int64_t   realized_pnl;
    int64_t   unrealized_pnl;
    int64_t   total_pnl;
    int64_t   volume;
    uint32_t  position_count;
    uint32_t  winning_markets;
    uint32_t  losing_markets;
};

// ---------------------------------------------------------------------------
// PositionKey — identifies a position in the hash map
// ---------------------------------------------------------------------------
struct PositionKey {
    WalletIdx wallet_idx;
    TokenIdx  token_idx;

    bool operator==(const PositionKey& o) const noexcept {
        return wallet_idx == o.wallet_idx && token_idx == o.token_idx;
    }
};

// ---------------------------------------------------------------------------
// PositionMap — open-addressing flat hash map for positions.
// Keys: PositionKey (8 bytes). Values: Position (48 bytes).
// Load factor target: 0.7. No iterator invalidation on update (no resize on hot path).
// ---------------------------------------------------------------------------
class PositionMap {
public:
    static constexpr uint32_t INITIAL_CAPACITY = 1 << 20; // 1M slots
    static constexpr double   MAX_LOAD_FACTOR  = 0.70;

    PositionMap();
    ~PositionMap();

    // Returns pointer to existing or newly inserted position (zeroed on insert).
    // Returns nullptr only on severe OOM (should not happen in normal operation).
    Position* get_or_create(PositionKey key) noexcept;

    // Returns pointer to existing position, or nullptr if not found.
    Position* find(PositionKey key) noexcept;

    // Iterate over all non-empty positions.
    void for_each(std::function<void(PositionKey, Position&)> fn);

    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }

private:
    struct Slot {
        PositionKey key;
        Position    value;
        bool        occupied = false;
        uint8_t     _pad[7];
    };

    Slot*    slots_    = nullptr;
    size_t   size_     = 0;
    size_t   capacity_ = 0;

    static uint32_t hash(PositionKey k) noexcept {
        // FNV-1a-inspired mix
        uint64_t h = (uint64_t)k.wallet_idx * 2654435761ULL;
        h ^= (uint64_t)k.token_idx * 2246822519ULL;
        h ^= h >> 16;
        return static_cast<uint32_t>(h);
    }

    void grow();
};

// ---------------------------------------------------------------------------
// RollbackJournal — records per-block position deltas for reorg support
// ---------------------------------------------------------------------------
struct PositionDelta {
    PositionKey key;
    Position    before; // snapshot of position before the event
};

struct BlockJournalEntry {
    uint64_t                  block_number = 0;
    std::vector<PositionDelta> deltas;
};

class RollbackJournal {
public:
    static constexpr int WINDOW = 128; // ADR-007

    void record_before(uint64_t block, PositionKey key, const Position& before);
    // Returns all deltas for blocks >= rollback_block, in reverse order.
    std::vector<PositionDelta> pop_since(uint64_t rollback_block);
    void confirm_block(uint64_t block); // remove blocks outside the window

private:
    std::array<BlockJournalEntry, WINDOW> entries_;
    int head_ = 0; // ring buffer head

    int index_of(uint64_t block) const noexcept;
};

// ---------------------------------------------------------------------------
// TokenHolderIndex — per-token list of wallets with non-zero positions.
// Used to batch-apply mark updates efficiently.
// ---------------------------------------------------------------------------
class TokenHolderIndex {
public:
    friend class PnLEngine;
    void add_holder(TokenIdx token, WalletIdx wallet);
    void remove_holder(TokenIdx token, WalletIdx wallet);

    // For each holder of token, call fn(wallet_idx, position_size).
    void for_holders(TokenIdx token,
                     std::function<void(WalletIdx, int64_t size)> fn) const;

private:
    // Simple: vector<WalletIdx> per token. For production, use a compact set.
    std::vector<std::vector<WalletIdx>> holders_; // indexed by TokenIdx
    void ensure_token(TokenIdx token);
};

// ---------------------------------------------------------------------------
// PnLEngine — the main engine class
// ---------------------------------------------------------------------------
class PnLEngine {
public:
    PnLEngine();
    ~PnLEngine() = default;

    // Process a single event. Must be called in strict (block, log_index) order.
    // Returns false if the event was invalid/malformed (counted in metrics).
    bool process_event(const NormalizedEvent& ev) noexcept;

    // Process a batch of events (more efficient; avoids per-call overhead).
    void process_events(const NormalizedEvent* events, int count) noexcept;

    // Apply a mark price update for a token.
    // Coalesces into ~100ms batches internally; returns immediately.
    void update_mark(TokenIdx token_idx, int64_t new_mark_price) noexcept;

    // Flush any pending mark updates immediately (call before snapshot).
    void flush_marks() noexcept;

    // Rollback all events from blocks >= rollback_from_block.
    // Resets position state to what it was before those blocks.
    void rollback(uint64_t rollback_from_block);

    // Query
    WalletSummary   get_wallet_summary(WalletIdx wallet_idx) const noexcept;
    Position*       get_position(WalletIdx wallet, TokenIdx token) noexcept;

    // Leaderboard: returns top-K entries sorted by |metric|
    std::vector<LeaderboardEntry> get_leaderboard(int k = 100) const;

    // Snapshot support
    uint64_t last_processed_block() const noexcept { return last_block_; }
    uint64_t last_processed_log_index() const noexcept { return last_log_index_; }

    // Metrics (for Prometheus)
    uint64_t events_processed()  const noexcept { return stats_.events_processed; }
    uint64_t fills_processed()   const noexcept { return stats_.fills_processed; }
    uint64_t rollbacks_applied() const noexcept { return stats_.rollbacks_applied; }
    uint64_t incomplete_hist()   const noexcept { return stats_.incomplete_history_events; }

private:
    // --- Hot-path event handlers ---
    void handle_fill(const NormalizedEvent& ev) noexcept;
    void handle_split(const NormalizedEvent& ev) noexcept;
    void handle_merge(const NormalizedEvent& ev) noexcept;
    void handle_redeem(const NormalizedEvent& ev) noexcept;
    void handle_transfer_in(const NormalizedEvent& ev) noexcept;
    void handle_transfer_out(const NormalizedEvent& ev) noexcept;
    void handle_resolution(const NormalizedEvent& ev) noexcept;

    // --- Accounting helpers ---
    // Apply a buy: updates avg_price and size. Records rollback delta.
    void apply_buy(PositionKey key, int64_t qty, int64_t price, int32_t fee,
                   uint64_t block, bool synthetic) noexcept;

    // Apply a sell: realizes PnL, reduces size. Records rollback delta.
    void apply_sell(PositionKey key, int64_t qty, int64_t price, int32_t fee,
                    uint64_t block, bool synthetic) noexcept;

    // --- State ---
    PositionMap         positions_;
    TokenHolderIndex    holder_index_;
    RollbackJournal     journal_;

    // Current mark prices per token (1e-6 USDC)
    std::vector<int64_t> marks_; // indexed by TokenIdx

    // Pending mark deltas (coalesced before flush)
    struct PendingMark {
        TokenIdx token_idx;
        int64_t  new_price;
    };
    std::vector<PendingMark> pending_marks_;

    uint64_t last_block_      = 0;
    uint64_t last_log_index_  = 0;

    // Per-wallet running totals (for fast leaderboard)
    struct WalletTotals {
        int64_t  realized_pnl   = 0;
        int64_t  unrealized_pnl = 0;
        int64_t  volume         = 0;
        uint32_t winning_markets = 0;
        uint32_t losing_markets  = 0;
        uint32_t position_count  = 0;
    };
    std::vector<WalletTotals> wallet_totals_; // indexed by WalletIdx

    // Statistics
    struct Stats {
        uint64_t events_processed          = 0;
        uint64_t fills_processed           = 0;
        uint64_t rollbacks_applied         = 0;
        uint64_t incomplete_history_events = 0;
        uint64_t invalid_events            = 0;
    } stats_;

    void ensure_wallet_totals(WalletIdx idx);
    void ensure_marks(TokenIdx idx);
    int64_t current_mark(TokenIdx idx) const noexcept;
};

} // namespace engine
} // namespace polypnl
