#include "engine.h"
#include <algorithm>
#include <cstring>
#include <cassert>
#include <new>

// ============================================================================
// PnL Engine implementation
//
// All accounting formulas are documented in docs/ACCOUNTING_MODEL.md.
// All edge-case policies are in docs/EDGE_CASES.md.
// ============================================================================

namespace polypnl {
namespace engine {

// ---------------------------------------------------------------------------
// PositionMap implementation — open-addressing flat hash map
// ---------------------------------------------------------------------------

PositionMap::PositionMap() {
    capacity_ = INITIAL_CAPACITY;
    slots_ = new (std::nothrow) Slot[capacity_]();
    if (!slots_) capacity_ = 0;
}

PositionMap::~PositionMap() {
    delete[] slots_;
}

Position* PositionMap::get_or_create(PositionKey key) noexcept {
    if (!slots_) return nullptr;

    // Grow if needed
    if (size_ + 1 > static_cast<size_t>(capacity_ * MAX_LOAD_FACTOR))
        grow();
    if (!slots_) return nullptr;

    uint32_t h = hash(key);
    size_t mask = capacity_ - 1;
    size_t pos = h & mask;

    while (true) {
        Slot& s = slots_[pos];
        if (!s.occupied) {
            // Insert new entry
            s.key      = key;
            s.occupied = true;
            std::memset(&s.value, 0, sizeof(Position));
            ++size_;
            return &s.value;
        }
        if (s.key == key) return &s.value;
        pos = (pos + 1) & mask;
    }
}

Position* PositionMap::find(PositionKey key) noexcept {
    if (!slots_) return nullptr;
    uint32_t h = hash(key);
    size_t mask = capacity_ - 1;
    size_t pos = h & mask;

    for (size_t i = 0; i < capacity_; ++i) {
        Slot& s = slots_[pos];
        if (!s.occupied) return nullptr;
        if (s.key == key) return &s.value;
        pos = (pos + 1) & mask;
    }
    return nullptr;
}

void PositionMap::for_each(std::function<void(PositionKey, Position&)> fn) {
    for (size_t i = 0; i < capacity_; ++i) {
        if (slots_[i].occupied)
            fn(slots_[i].key, slots_[i].value);
    }
}

void PositionMap::grow() {
    size_t new_cap = capacity_ * 2;
    Slot* new_slots = new (std::nothrow) Slot[new_cap]();
    if (!new_slots) return;

    size_t mask = new_cap - 1;
    for (size_t i = 0; i < capacity_; ++i) {
        Slot& old = slots_[i];
        if (!old.occupied) continue;
        uint32_t h = hash(old.key);
        size_t pos = h & mask;
        while (new_slots[pos].occupied) pos = (pos + 1) & mask;
        new_slots[pos] = old;
    }

    delete[] slots_;
    slots_    = new_slots;
    capacity_ = new_cap;
}

// ---------------------------------------------------------------------------
// RollbackJournal implementation
// ---------------------------------------------------------------------------

void RollbackJournal::record_before(uint64_t block, PositionKey key, const Position& before) {
    // Find or create entry for this block
    int idx = index_of(block);
    if (idx < 0) {
        // Add new entry at head
        head_ = (head_ + 1) % WINDOW;
        entries_[head_].block_number = block;
        entries_[head_].deltas.clear();
        idx = head_;
    }
    // Check if this key is already recorded for this block (only record first state)
    for (auto& d : entries_[idx].deltas) {
        if (d.key == key) return; // already recorded
    }
    entries_[idx].deltas.push_back({key, before});
}

std::vector<PositionDelta> RollbackJournal::pop_since(uint64_t rollback_block) {
    std::vector<PositionDelta> result;
    // Collect all deltas from blocks >= rollback_block
    for (int i = 0; i < WINDOW; ++i) {
        auto& e = entries_[i];
        if (e.block_number >= rollback_block && !e.deltas.empty()) {
            for (auto& d : e.deltas) result.push_back(d);
            e.block_number = 0;
            e.deltas.clear();
        }
    }
    return result;
}

void RollbackJournal::confirm_block(uint64_t block) {
    // Remove journal entries for blocks older than WINDOW
    if (block < WINDOW) return;
    uint64_t cutoff = block - WINDOW;
    for (auto& e : entries_) {
        if (e.block_number > 0 && e.block_number <= cutoff) {
            e.block_number = 0;
            e.deltas.clear();
        }
    }
}

int RollbackJournal::index_of(uint64_t block) const noexcept {
    for (int i = 0; i < WINDOW; ++i) {
        if (entries_[i].block_number == block) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// TokenHolderIndex implementation
// ---------------------------------------------------------------------------

void TokenHolderIndex::ensure_token(TokenIdx token) {
    if (token >= holders_.size())
        holders_.resize(token + 1);
}

void TokenHolderIndex::add_holder(TokenIdx token, WalletIdx wallet) {
    ensure_token(token);
    auto& vec = holders_[token];
    for (auto w : vec) if (w == wallet) return; // already in
    vec.push_back(wallet);
}

void TokenHolderIndex::remove_holder(TokenIdx token, WalletIdx wallet) {
    if (token >= holders_.size()) return;
    auto& vec = holders_[token];
    for (size_t i = 0; i < vec.size(); ++i) {
        if (vec[i] == wallet) {
            vec[i] = vec.back();
            vec.pop_back();
            return;
        }
    }
}

void TokenHolderIndex::for_holders(TokenIdx token,
                                    std::function<void(WalletIdx, int64_t)> fn) const {
    if (token >= holders_.size()) return;
    // We need to look up position sizes; this is called with the engine's position map
    // The fn receives (wallet_idx, ignored_size) and must look up size itself.
    // For the batched mark update path, the caller queries position.size directly.
    for (WalletIdx w : holders_[token]) fn(w, 0);
}

// ---------------------------------------------------------------------------
// PnLEngine implementation
// ---------------------------------------------------------------------------

PnLEngine::PnLEngine() = default;

void PnLEngine::ensure_wallet_totals(WalletIdx idx) {
    if (idx >= wallet_totals_.size())
        wallet_totals_.resize(idx + 1);
}

void PnLEngine::ensure_marks(TokenIdx idx) {
    if (idx >= marks_.size())
        marks_.resize(idx + 1, 0);
}

int64_t PnLEngine::current_mark(TokenIdx idx) const noexcept {
    if (idx >= marks_.size()) return 0;
    return marks_[idx];
}

bool PnLEngine::process_event(const NormalizedEvent& ev) noexcept {
    if (ev.wallet_idx == INVALID_WALLET || ev.token_idx == INVALID_TOKEN) {
        ++stats_.invalid_events;
        return false;
    }

    ++stats_.events_processed;
    last_block_     = ev.block_number;
    last_log_index_ = ev.log_index;
    journal_.confirm_block(ev.block_number);

    switch (ev.kind) {
        case EventKind::FILL:
            handle_fill(ev);
            ++stats_.fills_processed;
            break;
        case EventKind::SPLIT:         handle_split(ev);         break;
        case EventKind::MERGE:         handle_merge(ev);         break;
        case EventKind::REDEEM:        handle_redeem(ev);        break;
        case EventKind::TRANSFER_IN:   handle_transfer_in(ev);   break;
        case EventKind::TRANSFER_OUT:  handle_transfer_out(ev);  break;
        case EventKind::RESOLUTION:    handle_resolution(ev);    break;
        case EventKind::MARK_UPDATE:
            update_mark(ev.token_idx, ev.price);
            break;
        default: break;
    }
    return true;
}

void PnLEngine::process_events(const NormalizedEvent* events, int count) noexcept {
    for (int i = 0; i < count; ++i)
        process_event(events[i]);
}

// ---------------------------------------------------------------------------
// apply_buy — average cost basis update on a purchase
// ---------------------------------------------------------------------------
void PnLEngine::apply_buy(PositionKey key, int64_t qty, int64_t price, int32_t fee,
                           uint64_t block, bool synthetic) noexcept {
    if (qty <= 0) return;
    ensure_wallet_totals(key.wallet_idx);
    ensure_marks(key.token_idx);

    Position* pos = positions_.get_or_create(key);
    if (!pos) return;

    // Record pre-event state in rollback journal
    if (!synthetic)
        journal_.record_before(block, key, *pos);

    // Update holder index
    if (pos->size == 0)
        holder_index_.add_holder(key.token_idx, key.wallet_idx);

    // Average cost basis update
    // new_avg = (old_size * old_avg + qty * price) / (old_size + qty)
    // Use __int128 to avoid overflow (see ACCOUNTING_MODEL.md section 4)
    __int128 numerator = (__int128)pos->size * pos->avg_price + (__int128)qty * price;
    int64_t  new_size  = pos->size + qty;
    pos->avg_price = (new_size > 0) ? static_cast<int64_t>(numerator / new_size) : price;
    pos->size      = new_size;

    // Fees reduce realized PnL
    pos->realized_pnl -= fee;
    wallet_totals_[key.wallet_idx].realized_pnl -= fee;

    // Update running unrealized: add qty * (mark - price) / 1e6
    int64_t mark = current_mark(key.token_idx);
    pos->running_unrealized += (__int128)qty * (mark - price) / 1'000'000LL;

    // Update wallet volume
    wallet_totals_[key.wallet_idx].volume += (__int128)qty * price / 1'000'000LL;
    ++wallet_totals_[key.wallet_idx].position_count;

    if (synthetic) pos->is_synthetic = true;
}

// ---------------------------------------------------------------------------
// apply_sell — realize PnL on a sale
// ---------------------------------------------------------------------------
void PnLEngine::apply_sell(PositionKey key, int64_t qty, int64_t price, int32_t fee,
                            uint64_t block, bool synthetic) noexcept {
    if (qty <= 0) return;
    ensure_wallet_totals(key.wallet_idx);
    ensure_marks(key.token_idx);

    Position* pos = positions_.get_or_create(key);
    if (!pos) return;

    if (!synthetic)
        journal_.record_before(block, key, *pos);

    // Edge case: selling more than tracked size (EC-008)
    int64_t actual_qty = qty;
    if (qty > pos->size) {
        actual_qty = pos->size;
        pos->incomplete_history = true;
        ++stats_.incomplete_history_events;
    }

    if (actual_qty <= 0) return;

    // Realize PnL: qty * (price - avg_price) / 1e6 (already in 1e6 scale)
    int64_t gain = static_cast<int64_t>((__int128)actual_qty * (price - pos->avg_price) / 1'000'000LL);
    pos->realized_pnl += gain;
    pos->realized_pnl -= fee;
    wallet_totals_[key.wallet_idx].realized_pnl += (gain - fee);

    // Update unrealized
    pos->running_unrealized -= static_cast<int64_t>((__int128)actual_qty * (current_mark(key.token_idx) - pos->avg_price) / 1'000'000LL);

    // Reduce size
    pos->size -= actual_qty;
    if (pos->size == 0) {
        pos->avg_price = 0;
        pos->running_unrealized = 0;
        holder_index_.remove_holder(key.token_idx, key.wallet_idx);
        --wallet_totals_[key.wallet_idx].position_count;
    }

    // Volume
    wallet_totals_[key.wallet_idx].volume += (__int128)actual_qty * price / 1'000'000LL;

    if (synthetic) pos->is_synthetic = true;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void PnLEngine::handle_fill(const NormalizedEvent& ev) noexcept {
    PositionKey key{ev.wallet_idx, ev.token_idx};
    if (ev.side == FillSide::BUY)
        apply_buy(key, ev.qty, ev.price, ev.fee, ev.block_number, false);
    else
        apply_sell(key, ev.qty, ev.price, ev.fee, ev.block_number, false);
}

void PnLEngine::handle_split(const NormalizedEvent& ev) noexcept {
    // SPLIT: treat as buying qty/2 YES and qty/2 NO at $0.50 each.
    // token_idx is the conditionId group key; engine must resolve to YES/NO tokens.
    // For v1 (single-threaded), we store the split against the conditionId directly.
    // The API layer resolves conditionId → YES/NO via metadata for display.
    // Cost basis: price = 0.50 per token (500_000 in 1e6 scale)
    PositionKey key{ev.wallet_idx, ev.token_idx};
    // For a binary split of 'qty' collateral: each side gets 'qty' tokens at $0.50 basis
    // (aux_token_idx carries the complementary side if set)
    apply_buy(key, ev.qty, 500'000, 0, ev.block_number, false);
    if (ev.aux_token_idx != INVALID_TOKEN) {
        PositionKey aux_key{ev.wallet_idx, ev.aux_token_idx};
        apply_buy(aux_key, ev.qty, 500'000, 0, ev.block_number, false);
    }
    if (ev.flags & EventFlags::NEGRISK) {
        Position* pos = positions_.find(key);
        if (pos) pos->has_negrisk = true;
    }
}

void PnLEngine::handle_merge(const NormalizedEvent& ev) noexcept {
    // MERGE: treat as selling qty YES and qty NO at $0.50
    PositionKey key{ev.wallet_idx, ev.token_idx};
    apply_sell(key, ev.qty, 500'000, 0, ev.block_number, false);
    if (ev.aux_token_idx != INVALID_TOKEN) {
        PositionKey aux_key{ev.wallet_idx, ev.aux_token_idx};
        apply_sell(aux_key, ev.qty, 500'000, 0, ev.block_number, false);
    }
}

void PnLEngine::handle_redeem(const NormalizedEvent& ev) noexcept {
    // REDEEM: sell at payout_price (1_000_000 for winner, 0 for loser)
    PositionKey key{ev.wallet_idx, ev.token_idx};
    Position* pos = positions_.find(key);
    int64_t redeem_qty = (pos && pos->size > 0) ? pos->size : ev.qty;
    int32_t payout = ev.payout_price;
    apply_sell(key, redeem_qty, payout, 0, ev.block_number, false);
    if (pos) pos->is_resolved = true;

    // Update win rate
    ensure_wallet_totals(ev.wallet_idx);
    if (pos && pos->realized_pnl > 0)
        ++wallet_totals_[ev.wallet_idx].winning_markets;
    else if (pos && pos->realized_pnl < 0)
        ++wallet_totals_[ev.wallet_idx].losing_markets;
}

void PnLEngine::handle_transfer_in(const NormalizedEvent& ev) noexcept {
    // Transfer IN: zero cost basis (EC-005)
    PositionKey key{ev.wallet_idx, ev.token_idx};
    apply_buy(key, ev.qty, 0, 0, ev.block_number, false);
    Position* pos = positions_.find(key);
    if (pos) pos->has_transfers = true;
}

void PnLEngine::handle_transfer_out(const NormalizedEvent& ev) noexcept {
    // Transfer OUT: sell at current mark (EC-005)
    PositionKey key{ev.wallet_idx, ev.token_idx};
    int64_t mark = current_mark(ev.token_idx);
    apply_sell(key, ev.qty, mark, 0, ev.block_number, false);
    Position* pos = positions_.find(key);
    if (pos) pos->has_transfers = true;
}

void PnLEngine::handle_resolution(const NormalizedEvent& ev) noexcept {
    // Market resolved: synthetic REDEEM for all holders (EC-003)
    // token_idx here = conditionId group; engine must iterate all tokens in that market
    // Simplified: apply_sell at payout_price for this token's position
    PositionKey key{ev.wallet_idx, ev.token_idx};
    Position* pos = positions_.find(key);
    if (!pos || pos->size == 0 || pos->is_resolved) return;

    apply_sell(key, pos->size, ev.payout_price, 0, ev.block_number, true);
    pos->is_resolved = true;
    pos->is_synthetic = true;
}

// ---------------------------------------------------------------------------
// Mark updates — batched for efficiency
// ---------------------------------------------------------------------------

void PnLEngine::update_mark(TokenIdx token_idx, int64_t new_mark_price) noexcept {
    pending_marks_.push_back({token_idx, new_mark_price});
    // Flush automatically when batch size exceeds threshold
    if (pending_marks_.size() >= 1000)
        flush_marks();
}

void PnLEngine::flush_marks() noexcept {
    for (auto& pm : pending_marks_) {
        ensure_marks(pm.token_idx);
        int64_t old_mark = marks_[pm.token_idx];
        int64_t delta    = pm.new_price - old_mark;
        marks_[pm.token_idx] = pm.new_price;

        if (delta == 0) continue;

        // Apply delta to all holders' running_unrealized
        // For each holder: running_unrealized += size * delta / 1e6
        if (pm.token_idx < holder_index_.holders_.size()) {
            for (WalletIdx w : holder_index_.holders_[pm.token_idx]) {
                PositionKey key{w, pm.token_idx};
                Position* pos = positions_.find(key);
                if (!pos || pos->size == 0) continue;
                pos->running_unrealized += static_cast<int64_t>(
                    (__int128)pos->size * delta / 1'000'000LL);
                ensure_wallet_totals(w);
                wallet_totals_[w].unrealized_pnl = pos->running_unrealized; // approx
            }
        }
    }
    pending_marks_.clear();
}

// ---------------------------------------------------------------------------
// Rollback
// ---------------------------------------------------------------------------

void PnLEngine::rollback(uint64_t rollback_from_block) {
    flush_marks();
    auto deltas = journal_.pop_since(rollback_from_block);

    // Restore positions to their pre-event state
    for (auto& d : deltas) {
        Position* pos = positions_.find(d.key);
        if (!pos) continue;
        // Adjust holder index
        bool was_nonzero = d.before.size > 0;
        bool is_nonzero  = pos->size > 0;
        if (was_nonzero && !is_nonzero)
            holder_index_.add_holder(d.key.token_idx, d.key.wallet_idx);
        else if (!was_nonzero && is_nonzero)
            holder_index_.remove_holder(d.key.token_idx, d.key.wallet_idx);
        *pos = d.before;
    }

    ++stats_.rollbacks_applied;
    if (rollback_from_block > 0)
        last_block_ = rollback_from_block - 1;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

WalletSummary PnLEngine::get_wallet_summary(WalletIdx wallet_idx) const noexcept {
    WalletSummary s{};
    s.wallet_idx = wallet_idx;
    if (wallet_idx >= wallet_totals_.size()) return s;
    const auto& t = wallet_totals_[wallet_idx];
    s.realized_pnl   = t.realized_pnl;
    s.unrealized_pnl = t.unrealized_pnl;
    s.total_pnl      = t.realized_pnl + t.unrealized_pnl;
    s.volume         = t.volume;
    s.position_count = t.position_count;
    s.winning_markets = t.winning_markets;
    s.losing_markets  = t.losing_markets;
    return s;
}

Position* PnLEngine::get_position(WalletIdx wallet, TokenIdx token) noexcept {
    return positions_.find({wallet, token});
}

std::vector<LeaderboardEntry> PnLEngine::get_leaderboard(int k) const {
    std::vector<LeaderboardEntry> all;
    all.reserve(wallet_totals_.size());

    for (size_t i = 0; i < wallet_totals_.size(); ++i) {
        const auto& t = wallet_totals_[i];
        if (t.realized_pnl == 0 && t.unrealized_pnl == 0) continue;
        LeaderboardEntry e{};
        e.wallet_idx    = static_cast<WalletIdx>(i);
        e.realized_pnl  = t.realized_pnl;
        e.unrealized_pnl = t.unrealized_pnl;
        e.total_pnl     = t.realized_pnl + t.unrealized_pnl;
        e.volume        = t.volume;
        e.winning_markets = t.winning_markets;
        e.total_markets = t.winning_markets + t.losing_markets;
        all.push_back(e);
    }

    // Partial sort for top-K by total_pnl
    if (k > 0 && static_cast<int>(all.size()) > k) {
        std::partial_sort(all.begin(), all.begin() + k, all.end(),
            [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
                return a.total_pnl > b.total_pnl;
            });
        all.resize(k);
    } else {
        std::sort(all.begin(), all.end(),
            [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
                return a.total_pnl > b.total_pnl;
            });
    }

    return all;
}

} // namespace engine
} // namespace polypnl
