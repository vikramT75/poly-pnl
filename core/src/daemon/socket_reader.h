#pragma once

#include <string>
#include <atomic>
#include <thread>
#include "decoder.h"
#include "engine.h"
#include "intern_table.h"

namespace polypnl {
namespace daemon {

class SocketReader {
public:
    SocketReader(std::string socket_path,
                 decoder::Decoder& decoder,
                 engine::PnLEngine& engine,
                 TokenInternTable& token_table);
    ~SocketReader();

    void start();
    void stop();

private:
    void run();

    std::string socket_path_;
    decoder::Decoder& decoder_;
    engine::PnLEngine& engine_;
    TokenInternTable& token_table_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

} // namespace daemon
} // namespace polypnl
