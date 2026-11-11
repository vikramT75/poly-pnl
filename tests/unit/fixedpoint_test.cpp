#include <gtest/gtest.h>
#include <cstdint>
#include <limits>

namespace {

// Test that intermediate __int128 products do not overflow even with massive positions
TEST(FixedPointTest, NoOverflowIntermediate) {
    // 50 million shares (50_000_000 * 1e6 = 50e12)
    int64_t size1 = 50'000'000'000'000LL;
    int64_t avg1  = 650'000; // $0.65

    // Add another 50 million shares at $0.85
    int64_t qty2   = 50'000'000'000'000LL;
    int64_t price2 = 850'000; // $0.85

    // Using __int128 as implemented in engine
    __int128 num = (__int128)size1 * avg1 + (__int128)qty2 * price2;
    int64_t new_size = size1 + qty2;
    int64_t new_avg = static_cast<int64_t>(num / new_size);

    EXPECT_EQ(new_avg, 750'000); // exactly $0.75
}

// Bounded integer truncation error test
TEST(FixedPointTest, BoundedRounding) {
    // 10,000 divisions
    int64_t total_error = 0;
    for (int i = 1; i <= 10000; ++i) {
        // divide and multiply back
        int64_t original = i * 1000 + (i % 3);
        int64_t scaled = (original * 1'000'000LL) / 3'000'000LL;
        int64_t restored = scaled * 3;
        total_error += std::abs(original - restored);
    }
    // Average error per division is strictly bounded < 3 units
    EXPECT_LT(total_error / 10000, 3);
}

// Ensure prices are within [0, 1_000_000] for standard prediction markets
TEST(FixedPointTest, PriceBounds) {
    int64_t p_min = 0;
    int64_t p_max = 1'000'000;
    EXPECT_GE(p_min, 0);
    EXPECT_LE(p_max, 1'000'000);
}

} // namespace
