#include "snapshot.h"
#include <fstream>
#include <vector>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace gravia {
namespace daemon {

static constexpr uint32_t SNAPSHOT_MAGIC = 0x47525641; // "GRVA"
static constexpr uint32_t SNAPSHOT_VERSION = 1;

bool SnapshotManager::save_snapshot(const engine::PnLEngine& engine, uint64_t block_number, const std::string& filename) {
    if (!fs::exists(snapshot_dir_)) {
        fs::create_directories(snapshot_dir_);
    }

    std::string fname = filename.empty() ?
        (snapshot_dir_ + "/snapshot_" + std::to_string(block_number) + ".bin") : filename;
    std::string tmp_fname = fname + ".tmp";

    std::ofstream out(tmp_fname, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open temporary snapshot file for write: " << tmp_fname << std::endl;
        return false;
    }

    SnapshotHeader hdr{};
    hdr.magic = SNAPSHOT_MAGIC;
    hdr.version = SNAPSHOT_VERSION;
    hdr.block_number = block_number;
    hdr.timestamp_s = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    hdr.position_count = 0;
    hdr.checksum = 0;

    // Placeholder write for header
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    uint64_t record_count = 0;
    uint64_t running_chk = hdr.magic ^ hdr.version ^ hdr.block_number;

    // Stream out positions
    // PnLEngine get_leaderboard provides all active wallets or we iterate positions
    auto lb = engine.get_leaderboard(0); // get all non-zero wallets
    for (const auto& entry : lb) {
        PositionSnapshotRecord rec{};
        rec.wallet_idx = entry.wallet_idx;
        rec.token_idx = 0;
        rec.realized_pnl = entry.realized_pnl;
        rec.running_unrealized = entry.unrealized_pnl;
        rec.size = 0;
        rec.avg_price = 0;
        rec.incomplete_history = 0;
        rec.has_transfers = 0;
        rec.has_negrisk = 0;
        rec.is_resolved = 0;

        out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
        record_count++;
        running_chk ^= rec.realized_pnl ^ rec.wallet_idx;
    }

    hdr.position_count = record_count;
    hdr.checksum = running_chk;

    // Rewrite header with counts and checksum
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.close();

    // Atomic rename
    std::error_code ec;
    fs::rename(tmp_fname, fname, ec);
    if (ec) {
        std::cerr << "Failed to rename snapshot: " << ec.message() << std::endl;
        return false;
    }

    std::cout << "Successfully saved snapshot to " << fname << " (records: " << record_count << ")" << std::endl;
    return true;
}

bool SnapshotManager::load_latest_snapshot(engine::PnLEngine& engine, uint64_t& out_block_number) {
    if (!fs::exists(snapshot_dir_)) return false;

    std::string latest_file;
    uint64_t max_block = 0;

    for (const auto& entry : fs::directory_iterator(snapshot_dir_)) {
        if (entry.path().extension() == ".bin") {
            std::string stem = entry.path().stem().string();
            if (stem.rfind("snapshot_", 0) == 0 && stem.length() > 9) {
                char* end = nullptr;
                uint64_t b = std::strtoull(stem.c_str() + 9, &end, 10);
                if (end != stem.c_str() + 9 && b >= max_block) {
                    max_block = b;
                    latest_file = entry.path().string();
                }
            }
        }
    }

    if (latest_file.empty()) return false;

    std::ifstream in(latest_file, std::ios::binary);
    if (!in.is_open()) return false;

    SnapshotHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (hdr.magic != SNAPSHOT_MAGIC || hdr.version != SNAPSHOT_VERSION) {
        std::cerr << "Invalid snapshot header magic or version" << std::endl;
        return false;
    }

    out_block_number = hdr.block_number;
    std::cout << "Loaded snapshot from " << latest_file << " at block " << hdr.block_number << std::endl;
    return true;
}

} // namespace daemon
} // namespace gravia
