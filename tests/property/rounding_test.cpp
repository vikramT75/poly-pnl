#include <gtest/gtest.h>
#include <random>
#include "engine.h"

using namespace gravia;
using namespace gravia::engine;

namespace {

// Cumulative rounding error over N trades is bounded at +/- N micro-units
TEST(RoundingPropertyTest, CumulativeRoundingBounded) {
    PnLEngine engine;
    const WalletIdx W = 42;
    const TokenIdx  T = 7;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(400'000, 600'000);

    const int NUM_TRADES = 1000;
    int64_t total_in = 0;
    int64_t total_out = 0;

    for (int i = 0; i < NUM_TRADES; ++i) {
        int64_t p_buy = price_dist(rng);
        // Buy 1 share
        NormalizedEvent buy{};
        buy.kind = EventKind::FILL;
        buy.wallet_idx = W;
        buy.token_idx = T;
        buy.side = FillSide::BUY;
        buy.qty = 1'000'000;
        buy.price = p_buy;
        buy.block_number = i * 2 + 1;
        engine.process_event(buy);
        total_in += p_buy;

        // Sell 1 share
        int64_t p_sell = price_dist(rng);
        NormalizedEvent sell{};
        sell.kind = EventKind::FILL;
        sell.wallet_idx = W;
        sell.token_idx = T;
        sell.side = FillSide::SELL;
        sell.qty = 1'000'000;
        sell.price = p_sell;
        sell.block_number = i * 2 + 2;
        engine.process_event(sell);
        total_out += p_sell;
    }

    int64_t theoretical_pnl = total_out - total_in;
    Position* pos = engine.get_position(W, T);
    ASSERT_NE(pos, nullptr);
    int64_t engine_pnl = pos->realized_pnl;

    int64_t diff = std::abs(theoretical_pnl - engine_pnl);
    // Over 1000 trades, difference from theoretical exact is less than 1000 units (=$0.001)
    EXPECT_LE(diff, NUM_TRADES);
}

} // namespace
