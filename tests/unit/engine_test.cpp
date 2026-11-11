#include <gtest/gtest.h>
#include "engine.h"

using namespace polypnl;
using namespace polypnl::engine;

class EngineTest : public ::testing::Test {
protected:
    PnLEngine engine;
    const WalletIdx W1 = 1;
    const WalletIdx W2 = 2;
    const TokenIdx  T1 = 100;
    const TokenIdx  T2 = 200;

    NormalizedEvent make_fill(WalletIdx w, TokenIdx t, FillSide side,
                              int64_t qty, int64_t price, int32_t fee, uint64_t block) {
        NormalizedEvent ev{};
        ev.kind = EventKind::FILL;
        ev.wallet_idx = w;
        ev.token_idx = t;
        ev.side = side;
        ev.qty = qty;
        ev.price = price;
        ev.fee = fee;
        ev.block_number = block;
        ev.log_index = 0;
        return ev;
    }
};

// 1. Basic Buy
TEST_F(EngineTest, BasicBuy) {
    // Buy 1 share @ $0.60 with $0.001 fee
    NormalizedEvent buy = make_fill(W1, T1, FillSide::BUY, 1'000'000, 600'000, 1'000, 100);
    ASSERT_TRUE(engine.process_event(buy));

    Position* pos = engine.get_position(W1, T1);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->size, 1'000'000);
    EXPECT_EQ(pos->avg_price, 600'000);
    EXPECT_EQ(pos->realized_pnl, -1'000); // fee subtracted
}

// 2. Basic Sell with Profit
TEST_F(EngineTest, BasicSellWithProfit) {
    // Buy 1 share @ $0.60
    engine.process_event(make_fill(W1, T1, FillSide::BUY, 1'000'000, 600'000, 0, 100));
    // Sell 1 share @ $0.75
    engine.process_event(make_fill(W1, T1, FillSide::SELL, 1'000'000, 750'000, 500, 101));

    Position* pos = engine.get_position(W1, T1);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->size, 0); // closed
    // Profit = 1.0 * (0.75 - 0.60) = $0.15 = 150_000 minus 500 fee = 149_500
    EXPECT_EQ(pos->realized_pnl, 149'500);

    auto summary = engine.get_wallet_summary(W1);
    EXPECT_EQ(summary.realized_pnl, 149'500);
}

// 3. Average Cost Basis across multiple buys
TEST_F(EngineTest, AverageCostBasisMultipleBuys) {
    // Buy 100 shares @ $0.40
    engine.process_event(make_fill(W1, T1, FillSide::BUY, 100'000'000, 400'000, 0, 100));
    // Buy 100 shares @ $0.60
    engine.process_event(make_fill(W1, T1, FillSide::BUY, 100'000'000, 600'000, 0, 101));

    Position* pos = engine.get_position(W1, T1);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->size, 200'000'000);
    EXPECT_EQ(pos->avg_price, 500'000); // (100*0.40 + 100*0.60) / 200 = $0.50

    // Sell 50 shares @ $0.80
    engine.process_event(make_fill(W1, T1, FillSide::SELL, 50'000'000, 800'000, 0, 102));
    EXPECT_EQ(pos->size, 150'000'000);
    EXPECT_EQ(pos->avg_price, 500'000); // avg price unchanged by sell
    // Profit = 50 * (0.80 - 0.50) = $15.00 = 15_000_000
    EXPECT_EQ(pos->realized_pnl, 15'000'000);
}

// 4. Oversell Clamping (Edge case EC-008)
TEST_F(EngineTest, OversellClamping) {
    // Only 10 shares bought
    engine.process_event(make_fill(W1, T1, FillSide::BUY, 10'000'000, 500'000, 0, 100));

    // Try to sell 50 shares (incomplete history: bought 40 shares before window)
    engine.process_event(make_fill(W1, T1, FillSide::SELL, 50'000'000, 700'000, 0, 101));

    Position* pos = engine.get_position(W1, T1);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->size, 0); // clamped, never negative!
    EXPECT_TRUE(pos->incomplete_history);
    // Realized only on the 10 shares tracked: 10 * (0.70 - 0.50) = 2_000_000
    EXPECT_EQ(pos->realized_pnl, 2'000'000);
}

// 5. Split and Merge round trip conservation
TEST_F(EngineTest, SplitAndMergeConservation) {
    // Split 10 USDC into YES (T1) and NO (T2)
    NormalizedEvent split{};
    split.kind = EventKind::SPLIT;
    split.wallet_idx = W1;
    split.token_idx = T1;
    split.aux_token_idx = T2;
    split.qty = 10'000'000;
    split.price = 500'000; // $0.50 each
    split.block_number = 100;
    engine.process_event(split);

    Position* pos1 = engine.get_position(W1, T1);
    Position* pos2 = engine.get_position(W1, T2);
    ASSERT_NE(pos1, nullptr);
    ASSERT_NE(pos2, nullptr);
    EXPECT_EQ(pos1->size, 10'000'000);
    EXPECT_EQ(pos1->avg_price, 500'000);
    EXPECT_EQ(pos2->size, 10'000'000);
    EXPECT_EQ(pos2->avg_price, 500'000);

    // Merge them back
    NormalizedEvent merge{};
    merge.kind = EventKind::MERGE;
    merge.wallet_idx = W1;
    merge.token_idx = T1;
    merge.aux_token_idx = T2;
    merge.qty = 10'000'000;
    merge.price = 500'000;
    merge.block_number = 101;
    engine.process_event(merge);

    EXPECT_EQ(pos1->size, 0);
    EXPECT_EQ(pos2->size, 0);
    // Realized PnL is exactly 0 (no fee, round trip)
    EXPECT_EQ(pos1->realized_pnl, 0);
    EXPECT_EQ(pos2->realized_pnl, 0);
}

// 6. Rollback Restoration
TEST_F(EngineTest, RollbackRestoresState) {
    // Block 100: Buy 10 @ $0.50
    engine.process_event(make_fill(W1, T1, FillSide::BUY, 10'000'000, 500'000, 0, 100));

    // Block 101: Reorged block - Sell 10 @ $0.80
    engine.process_event(make_fill(W1, T1, FillSide::SELL, 10'000'000, 800'000, 0, 101));

    Position* pos = engine.get_position(W1, T1);
    EXPECT_EQ(pos->size, 0);
    EXPECT_EQ(pos->realized_pnl, 3'000'000);

    // Rollback block 101
    engine.rollback(101);

    // State should be exactly what it was after block 100!
    pos = engine.get_position(W1, T1);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->size, 10'000'000);
    EXPECT_EQ(pos->avg_price, 500'000);
    EXPECT_EQ(pos->realized_pnl, 0);
}

// 7. Mark Price and Unrealized PnL updates
TEST_F(EngineTest, MarkPriceUnrealized) {
    // Buy 100 shares @ $0.50
    engine.process_event(make_fill(W1, T1, FillSide::BUY, 100'000'000, 500'000, 0, 100));

    // Update mark price to $0.70
    engine.update_mark(T1, 700'000);
    engine.flush_marks();

    Position* pos = engine.get_position(W1, T1);
    ASSERT_NE(pos, nullptr);
    // Unrealized = 100 * (0.70 - 0.50) = $20.00 = 20_000_000
    EXPECT_EQ(pos->running_unrealized, 20'000'000);

    auto summary = engine.get_wallet_summary(W1);
    EXPECT_EQ(summary.unrealized_pnl, 20'000'000);
    EXPECT_EQ(summary.total_pnl, 20'000'000);
}

// 8. Market Resolution (Synthetic Redeem)
TEST_F(EngineTest, ResolutionSyntheticRedeem) {
    // Buy 10 shares @ $0.60
    engine.process_event(make_fill(W1, T1, FillSide::BUY, 10'000'000, 600'000, 0, 100));

    // Market resolves: Winner pays $1.00 (1_000_000)
    NormalizedEvent res{};
    res.kind = EventKind::RESOLUTION;
    res.wallet_idx = W1;
    res.token_idx = T1;
    res.payout_price = 1'000'000;
    res.block_number = 150;
    engine.process_event(res);

    Position* pos = engine.get_position(W1, T1);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->size, 0);
    EXPECT_TRUE(pos->is_resolved);
    EXPECT_TRUE(pos->is_synthetic);
    // Realized = 10 * (1.00 - 0.60) = $4.00 = 4_000_000
    EXPECT_EQ(pos->realized_pnl, 4'000'000);
}
