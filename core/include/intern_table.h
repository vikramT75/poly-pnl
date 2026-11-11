#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <mutex>
#include <shared_mutex>

// ============================================================================
// InternTable — maps variable-length byte keys to dense uint32 indices.
//
// Used to convert:
//   20-byte wallet addresses → WalletIdx (uint32)
//   32-byte token IDs        → TokenIdx  (uint32)
//
// Design:
//   - Two-phase lookup: open-addressing hash map → index
//   - Thread-safe: shared_mutex allows concurrent reads, exclusive writes
//   - Reverse lookup: index → original bytes (for API responses)
//   - No deletion (IDs are permanently assigned)
//   - Keys are stored contiguously for cache efficiency
// ============================================================================

namespace polypnl {

template <size_t KEY_SIZE>
class InternTable {
public:
    using Index = uint32_t;
    static constexpr Index INVALID = 0xFFFFFFFF;
    static constexpr size_t INITIAL_CAPACITY = 1 << 17; // 128K slots

    InternTable() { resize(INITIAL_CAPACITY); }

    // Returns the existing index for key, or assigns a new one.
    // Thread-safe (shared_mutex).
    [[nodiscard]] Index intern(const uint8_t key[KEY_SIZE]) noexcept {
        uint32_t h = hash(key);
        {
            std::shared_lock lock(mu_);
            Index idx = lookup_locked(key, h);
            if (idx != INVALID) return idx;
        }
        std::unique_lock lock(mu_);
        // Re-check under write lock (double-checked locking)
        Index idx = lookup_locked(key, h);
        if (idx != INVALID) return idx;
        return insert_locked(key, h);
    }

    // Returns existing index without inserting. Returns INVALID if not found.
    [[nodiscard]] Index find(const uint8_t key[KEY_SIZE]) const noexcept {
        uint32_t h = hash(key);
        std::shared_lock lock(mu_);
        return lookup_locked(key, h);
    }

    // Returns the original key bytes for a given index. Returns false if out of range.
    [[nodiscard]] bool reverse(Index idx, uint8_t out[KEY_SIZE]) const noexcept {
        std::shared_lock lock(mu_);
        if (idx >= keys_.size()) return false;
        std::memcpy(out, keys_[idx].data(), KEY_SIZE);
        return true;
    }

    size_t size() const noexcept {
        std::shared_lock lock(mu_);
        return next_index_;
    }

private:
    struct Slot {
        uint32_t hash_val = 0;
        Index    index    = INVALID;
    };

    mutable std::shared_mutex mu_;
    std::vector<Slot>                       slots_;
    std::vector<std::array<uint8_t, KEY_SIZE>> keys_; // reverse lookup
    size_t capacity_ = 0;
    size_t next_index_ = 0;

    static uint32_t hash(const uint8_t key[KEY_SIZE]) noexcept {
        // FNV-1a
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < KEY_SIZE; ++i)
            h = (h ^ key[i]) * 16777619u;
        return h ? h : 1u; // 0 reserved for empty
    }

    Index lookup_locked(const uint8_t key[KEY_SIZE], uint32_t h) const noexcept {
        size_t cap = capacity_;
        size_t mask = cap - 1;
        size_t pos = h & mask;
        for (size_t i = 0; i < cap; ++i) {
            const Slot& s = slots_[pos];
            if (s.hash_val == 0) return INVALID; // empty slot
            if (s.hash_val == h && s.index != INVALID &&
                std::memcmp(keys_[s.index].data(), key, KEY_SIZE) == 0)
                return s.index;
            pos = (pos + 1) & mask;
        }
        return INVALID;
    }

    Index insert_locked(const uint8_t key[KEY_SIZE], uint32_t h) noexcept {
        // Grow if needed (load > 0.70)
        if (next_index_ + 1 > capacity_ * 70 / 100)
            resize(capacity_ * 2);

        Index new_idx = static_cast<Index>(next_index_++);
        keys_.emplace_back();
        std::memcpy(keys_.back().data(), key, KEY_SIZE);

        // Insert into hash table
        size_t cap = capacity_;
        size_t mask = cap - 1;
        size_t pos = h & mask;
        while (slots_[pos].hash_val != 0)
            pos = (pos + 1) & mask;
        slots_[pos] = {h, new_idx};

        return new_idx;
    }

    void resize(size_t new_cap) {
        std::vector<Slot> new_slots(new_cap);
        size_t mask = new_cap - 1;
        for (auto& s : slots_) {
            if (s.hash_val == 0) continue;
            size_t pos = s.hash_val & mask;
            while (new_slots[pos].hash_val != 0) pos = (pos + 1) & mask;
            new_slots[pos] = s;
        }
        slots_    = std::move(new_slots);
        capacity_ = new_cap;
    }
};

// Convenience aliases
using WalletInternTable = InternTable<20>;
using TokenInternTable  = InternTable<32>;

} // namespace polypnl
