#include <chrono>
#include <iostream>
#include <vector>
#include <cstring>
#include "decoder.h"
#include "intern_table.h"

using namespace polypnl;
using namespace polypnl::decoder;

int main() {
    WalletInternTable wallet_table;
    TokenInternTable  token_table;

    struct InternContext {
        WalletInternTable* wallets;
        TokenInternTable*  tokens;
    } ctx{&wallet_table, &token_table};

    InternTables tables{};
    tables.intern_wallet = [](const uint8_t addr[20], void* ctx) noexcept -> WalletIdx {
        return static_cast<InternContext*>(ctx)->wallets->intern(addr);
    };
    tables.intern_token = [](const uint8_t tok[32], void* ctx) noexcept -> TokenIdx {
        return static_cast<InternContext*>(ctx)->tokens->intern(tok);
    };
    tables.ctx = &ctx;

    Decoder decoder(tables);

    // Prepare a synthetic V2 OrderFilled log
    RawLog log{};
    log.block_number = 86200000;
    log.log_index = 10;
    std::memcpy(log.address, CTF_EXCHANGE_V2, 20);
    std::memcpy(log.topics[0], TOPIC0_ORDER_FILLED_V2, 32);
    log.num_topics = 4;
    log.data_len = 224;
    log.data[31] = 0; // BUY
    log.data[63] = 1; // token id
    log.data[95] = 100; // maker amount
    log.data[127] = 100; // taker amount

    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];

    constexpr int ITERATIONS = 1'000'000;
    std::cout << "Benchmarking decoder throughput (" << ITERATIONS << " events)..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        log.log_index = i;
        int n = decoder.decode(log, events, Decoder::MAX_EVENTS_PER_LOG);
        (void)n;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ns_per_event = static_cast<double>(elapsed_ns) / ITERATIONS;
    double events_per_sec = (1e9 / ns_per_event);

    std::cout << "Elapsed: " << (elapsed_ns / 1e6) << " ms\n"
              << "Per event: " << ns_per_event << " ns\n"
              << "Throughput: " << static_cast<uint64_t>(events_per_sec) << " events/sec\n";

    return 0;
}
