#pragma once

#include <string>
#include <atomic>
#include <thread>
#include "engine.h"
#include "intern_table.h"

namespace polypnl {
namespace daemon {

class QueryServer {
public:
    QueryServer(int port,
                engine::PnLEngine& engine,
                WalletInternTable& wallet_table);
    ~QueryServer();

    void start();
    void stop();

private:
    void run();

    int port_;
    engine::PnLEngine& engine_;
    WalletInternTable& wallet_table_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

} // namespace daemon
} // namespace polypnl
