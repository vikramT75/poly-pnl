#include <chrono>
#include <iostream>
#include <vector>
#include "engine.h"

using namespace gravia;
using namespace gravia::engine;

int main() {
    PnLEngine engine;

    constexpr int ITERATIONS = 1'000'000;
    std::cout << "Benchmarking PnL engine throughput (" << ITERATIONS << " position updates)..." << std::endl;

    std::vector<NormalizedEvent> events;
    events.reserve(ITERATIONS);

    for (int i = 0; i < ITERATIONS; ++i) {
        NormalizedEvent ev{};
        ev.kind = EventKind::FILL;
        ev.block_number = i / 100 + 1;
        ev.log_index = i % 100;
        ev.wallet_idx = (i % 50000) + 1; // 50,000 distinct wallets
        ev.token_idx = (i % 10) + 1;     // 10 active tokens
        ev.qty = 100'000;
        ev.price = 550'000;
        ev.side = (i % 2 == 0) ? FillSide::BUY : FillSide::SELL;
        ev.fee = 100;
        events.push_back(ev);
    }

    auto start = std::chrono::high_resolution_clock::now();
    engine.process_events(events.data(), ITERATIONS);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ns_per_event = static_cast<double>(elapsed_ns) / ITERATIONS;
    double events_per_sec = (1e9 / ns_per_event);

    std::cout << "Elapsed: " << (elapsed_ns / 1e6) << " ms\n"
              << "Per update: " << ns_per_event << " ns\n"
              << "Throughput: " << static_cast<uint64_t>(events_per_sec) << " updates/sec\n";

    // Benchmark Leaderboard computation
    auto lb_start = std::chrono::high_resolution_clock::now();
    auto lb = engine.get_leaderboard(100);
    auto lb_end = std::chrono::high_resolution_clock::now();

    auto lb_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(lb_end - lb_start).count();
    std::cout << "Leaderboard top-100 over 50k wallets: " << (lb_ns / 1e3) << " us\n";

    return 0;
}
