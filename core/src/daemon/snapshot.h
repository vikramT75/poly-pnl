#pragma once

#include <cstdint>
#include <string>
#include "engine.h"

namespace gravia {
namespace daemon {

#pragma pack(push, 1)
struct SnapshotHeader {
    uint32_t magic;          // 0x47525641 ("GRVA")
    uint32_t version;        // 1
    uint64_t block_number;   // Polygon block number at snapshot
    uint64_t timestamp_s;    // Timestamp of snapshot
    uint64_t position_count; // Number of PositionSnapshotRecord entries
    uint64_t checksum;       // Simple CRC64 / XOR checksum
};

struct PositionSnapshotRecord {
    uint32_t wallet_idx;
    uint32_t token_idx;
    int64_t  size;
    int64_t  avg_price;
    int64_t  realized_pnl;
    int64_t  running_unrealized;
    uint8_t  incomplete_history;
    uint8_t  has_transfers;
    uint8_t  has_negrisk;
    uint8_t  is_resolved;
};
#pragma pack(pop)

class SnapshotManager {
public:
    explicit SnapshotManager(std::string snapshot_dir)
        : snapshot_dir_(std::move(snapshot_dir)) {}

    // Save current engine state to an atomic binary snapshot
    bool save_snapshot(const engine::PnLEngine& engine, uint64_t block_number, const std::string& filename = "");

    // Load state from the latest snapshot
    bool load_latest_snapshot(engine::PnLEngine& engine, uint64_t& out_block_number);

private:
    std::string snapshot_dir_;
};

} // namespace daemon
} // namespace gravia
