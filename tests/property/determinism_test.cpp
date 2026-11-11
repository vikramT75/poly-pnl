#include <gtest/gtest.h>
#include <random>
#include <vector>
#include "engine.h"

using namespace gravia;
using namespace gravia::engine;

namespace {

// Determinism Property 1: The same ordered event stream produces byte-identical engine state
TEST(PropertyTest, SameEventsProduceIdenticalState) {
    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<uint32_t> wallet_dist(1, 20);
    std::uniform_int_distribution<uint32_t> token_dist(1, 5);
    std::uniform_int_distribution<int64_t> qty_dist(100'000, 5'000'000);
    std::uniform_int_distribution<int64_t> price_dist(100'000, 900'000);

    std::vector<NormalizedEvent> events;
    for (uint64_t b = 1; b <= 50; ++b) {
        for (int i = 0; i < 20; ++i) {
            NormalizedEvent ev{};
            ev.kind = EventKind::FILL;
            ev.block_number = b;
            ev.log_index = i;
            ev.wallet_idx = wallet_dist(rng);
            ev.token_idx = token_dist(rng);
            ev.qty = qty_dist(rng);
            ev.price = price_dist(rng);
            ev.side = (rng() % 2 == 0) ? FillSide::BUY : FillSide::SELL;
            ev.fee = 100;
            events.push_back(ev);
        }
    }

    PnLEngine engine1;
    PnLEngine engine2;

    engine1.process_events(events.data(), static_cast<int>(events.size()));
    engine2.process_events(events.data(), static_cast<int>(events.size()));

    auto lb1 = engine1.get_leaderboard(100);
    auto lb2 = engine2.get_leaderboard(100);

    ASSERT_EQ(lb1.size(), lb2.size());
    for (size_t i = 0; i < lb1.size(); ++i) {
        EXPECT_EQ(lb1[i].wallet_idx, lb2[i].wallet_idx);
        EXPECT_EQ(lb1[i].total_pnl, lb2[i].total_pnl);
        EXPECT_EQ(lb1[i].realized_pnl, lb2[i].realized_pnl);
        EXPECT_EQ(lb1[i].volume, lb2[i].volume);
    }
}

// Determinism Property 2: Rollback and replay restores identical state
TEST(PropertyTest, RollbackAndReplayRestoresIdenticalState) {
    PnLEngine engine;
    std::vector<NormalizedEvent> block1_events;
    std::vector<NormalizedEvent> block2_events;

    for (int i = 0; i < 10; ++i) {
        NormalizedEvent ev{};
        ev.kind = EventKind::FILL;
        ev.block_number = 100;
        ev.log_index = i;
        ev.wallet_idx = 10 + i;
        ev.token_idx = 1;
        ev.side = FillSide::BUY;
        ev.qty = 1'000'000;
        ev.price = 500'000;
        block1_events.push_back(ev);
    }

    for (int i = 0; i < 10; ++i) {
        NormalizedEvent ev{};
        ev.kind = EventKind::FILL;
        ev.block_number = 101;
        ev.log_index = i;
        ev.wallet_idx = 10 + i;
        ev.token_idx = 1;
        ev.side = FillSide::SELL;
        ev.qty = 500'000;
        ev.price = 700'000;
        block2_events.push_back(ev);
    }

    // Process blocks 100 and 101
    engine.process_events(block1_events.data(), static_cast<int>(block1_events.size()));
    engine.process_events(block2_events.data(), static_cast<int>(block2_events.size()));

    auto lb_original = engine.get_leaderboard(50);

    // Rollback block 101
    engine.rollback(101);

    // Replay canonical block 101
    engine.process_events(block2_events.data(), static_cast<int>(block2_events.size()));

    auto lb_replayed = engine.get_leaderboard(50);

    ASSERT_EQ(lb_original.size(), lb_replayed.size());
    for (size_t i = 0; i < lb_original.size(); ++i) {
        EXPECT_EQ(lb_original[i].wallet_idx, lb_replayed[i].wallet_idx);
        EXPECT_EQ(lb_original[i].total_pnl, lb_replayed[i].total_pnl);
        EXPECT_EQ(lb_original[i].realized_pnl, lb_replayed[i].realized_pnl);
    }
}

} // namespace
