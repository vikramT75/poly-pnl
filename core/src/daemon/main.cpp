#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

#include "events.h"
#include "decoder.h"
#include "intern_table.h"
#include "engine.h"
#include "socket_reader.h"
#include "query_server.h"
#include "snapshot.h"

#ifdef _WIN32
  #include <winsock2.h>
#endif

using namespace gravia;

static std::atomic<bool> g_shutdown{false};

void signal_handler(int sig) {
    std::cout << "\n[EngineDaemon] Received signal " << sig << ", initiating clean shutdown..." << std::endl;
    g_shutdown = true;
}

// C-compatible callbacks for the decoder's InternTables struct
static WalletIdx intern_wallet_cb(const uint8_t addr[20], void* ctx) noexcept {
    auto* table = static_cast<WalletInternTable*>(ctx);
    return table->intern(addr);
}

static TokenIdx intern_token_cb(const uint8_t token_id[32], void* ctx) noexcept {
    auto* table = static_cast<TokenInternTable*>(ctx);
    return table->intern(token_id);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    std::cout << "========================================================\n"
              << "       gravia-pnl: High-Performance Polymarket PnL Engine \n"
              << "========================================================" << std::endl;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 1. Initialize intern tables
    WalletInternTable wallet_table;
    TokenInternTable  token_table;

    struct InternContext {
        WalletInternTable* wallets;
        TokenInternTable*  tokens;
    } intern_ctx{&wallet_table, &token_table};

    // 2. Setup decoder with intern callbacks
    decoder::InternTables intern_tables{};
    intern_tables.intern_wallet = [](const uint8_t addr[20], void* ctx) noexcept -> WalletIdx {
        return static_cast<InternContext*>(ctx)->wallets->intern(addr);
    };
    intern_tables.intern_token = [](const uint8_t tok[32], void* ctx) noexcept -> TokenIdx {
        return static_cast<InternContext*>(ctx)->tokens->intern(tok);
    };
    intern_tables.ctx = &intern_ctx;
    decoder::Decoder log_decoder(intern_tables);

    // 3. Initialize engine
    engine::PnLEngine pnl_engine;

    // 4. Check for existing snapshot and restore if found
    daemon::SnapshotManager snapshot_mgr("./snapshots");
    uint64_t recovered_block = 0;
    if (snapshot_mgr.load_latest_snapshot(pnl_engine, recovered_block)) {
        std::cout << "[EngineDaemon] Recovered state up to block " << recovered_block << std::endl;
    } else {
        std::cout << "[EngineDaemon] Starting with fresh in-memory state" << std::endl;
    }

    // 5. Start Ingestor Socket Reader (port 9999)
    daemon::SocketReader socket_reader("/tmp/gravia_engine.sock", log_decoder, pnl_engine);
    socket_reader.start();

    // 6. Start Query API Server (port 9998)
    daemon::QueryServer query_server(9998, pnl_engine, wallet_table);
    query_server.start();

    std::cout << "[EngineDaemon] Engine ready. Processing events..." << std::endl;

    // Periodic snapshot timer (e.g. every 60 seconds)
    auto last_snapshot_time = std::chrono::steady_clock::now();

    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_snapshot_time).count() >= 60) {
            uint64_t last_b = pnl_engine.last_processed_block();
            if (last_b > 0) {
                snapshot_mgr.save_snapshot(pnl_engine, last_b);
            }
            last_snapshot_time = now;
        }
    }

    std::cout << "[EngineDaemon] Stopping query server and socket reader..." << std::endl;
    query_server.stop();
    socket_reader.stop();

    // Final snapshot on clean exit
    uint64_t final_block = pnl_engine.last_processed_block();
    if (final_block > 0) {
        std::cout << "[EngineDaemon] Saving final snapshot at block " << final_block << "..." << std::endl;
        snapshot_mgr.save_snapshot(pnl_engine, final_block);
    }

    std::cout << "[EngineDaemon] Shutdown complete." << std::endl;
    return 0;
}
