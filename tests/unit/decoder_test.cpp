#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "decoder.h"
#include "intern_table.h"

using namespace gravia;
using namespace gravia::decoder;

namespace {

// Test fixture with local intern tables
class DecoderTest : public ::testing::Test {
protected:
    WalletInternTable wallet_table;
    TokenInternTable  token_table;
    InternTables      intern_tables;
    std::unique_ptr<Decoder> decoder;

    void SetUp() override {
        intern_tables.intern_wallet = [](const uint8_t addr[20], void* ctx) noexcept -> WalletIdx {
            return static_cast<WalletInternTable*>(ctx)->intern(addr);
        };
        intern_tables.intern_token = [](const uint8_t token[32], void* ctx) noexcept -> TokenIdx {
            return static_cast<TokenInternTable*>(ctx)->intern(token);
        };
        intern_tables.ctx = &wallet_table; // token_table can be accessed via separate wrapper if needed
        decoder = std::make_unique<Decoder>(intern_tables);
    }

    // Helper to build a RawLog
    RawLog make_log(const uint8_t contract_addr[20],
                     const uint8_t topic0[32],
                     const uint8_t maker[20],
                     const uint8_t taker[20],
                     const std::vector<uint8_t>& data) {
        RawLog log{};
        log.block_number = 50000000;
        log.timestamp_s = 1700000000;
        log.log_index = 42;
        log.tx_index = 10;
        std::memcpy(log.address, contract_addr, 20);
        std::memcpy(log.topics[0], topic0, 32);

        // topics[1] = dummy orderHash
        std::memset(log.topics[1], 0xAA, 32);

        // topics[2] = maker (left-padded with 12 zeros)
        std::memset(log.topics[2], 0, 12);
        std::memcpy(log.topics[2] + 12, maker, 20);

        // topics[3] = taker (left-padded with 12 zeros)
        std::memset(log.topics[3], 0, 12);
        std::memcpy(log.topics[3] + 12, taker, 20);

        log.num_topics = 4;
        log.data_len = static_cast<uint32_t>(data.size());
        std::memcpy(log.data, data.data(), data.size());
        return log;
    }
};

// Test V1 OrderFilled: Maker buys outcome tokens (makerAssetId == 0, paying USDC)
TEST_F(DecoderTest, FillDirection_V1_MakerBuys) {
    uint8_t maker_addr[20] = {0x11, 0x22};
    uint8_t taker_addr[20] = {0x33, 0x44};

    // 5 slots (160 bytes):
    // slot 0: makerAssetId = 0 (USDC)
    // slot 1: takerAssetId = 999 (Token ID)
    // slot 2: makerAmountFilled = 650_000 ($0.65 USDC)
    // slot 3: takerAmountFilled = 1_000_000 (1 share)
    // slot 4: fee = 1000
    std::vector<uint8_t> data(160, 0);
    // slot 1 low byte = 999
    data[62] = 0x03; data[63] = 0xE7;
    // slot 2: 650000 = 0x09EB10
    data[93] = 0x09; data[94] = 0xEB; data[95] = 0x10;
    // slot 3: 1000000 = 0x0F4240
    data[125] = 0x0F; data[126] = 0x42; data[127] = 0x40;
    // slot 4: 1000
    data[158] = 0x03; data[159] = 0xE8;

    RawLog log = make_log(CTF_EXCHANGE_V1, TOPIC0_ORDER_FILLED_V1, maker_addr, taker_addr, data);

    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];
    int count = decoder->decode(log, events, Decoder::MAX_EVENTS_PER_LOG);

    ASSERT_EQ(count, 2);
    // Maker bought
    EXPECT_EQ(events[0].kind, EventKind::FILL);
    EXPECT_EQ(events[0].side, FillSide::BUY);
    EXPECT_EQ(events[0].qty, 1000000);
    EXPECT_EQ(events[0].price, 650000); // 650000 / 1000000 * 1e6 = 650000 ($0.65)
    EXPECT_EQ(events[0].fee, 0); // maker pays 0 in V1
    EXPECT_TRUE(events[0].flags & EventFlags::MAKER);

    // Taker sold
    EXPECT_EQ(events[1].kind, EventKind::FILL);
    EXPECT_EQ(events[1].side, FillSide::SELL);
    EXPECT_EQ(events[1].qty, 1000000);
    EXPECT_EQ(events[1].price, 650000);
    EXPECT_EQ(events[1].fee, 1000); // taker pays fee
}

// Test V2 OrderFilled: Side 0 (BUY)
TEST_F(DecoderTest, FillDirection_V2_Buy) {
    uint8_t maker_addr[20] = {0xAA, 0xBB};
    uint8_t taker_addr[20] = {0xCC, 0xDD};

    // 7 slots (224 bytes):
    // slot 0: side = 0 (BUY) -> byte 31 = 0
    // slot 1: tokenId = 12345
    // slot 2: makerAmountFilled = 600_000 (collateral paid)
    // slot 3: takerAmountFilled = 1_000_000 (tokens received)
    // slot 4: fee = 2000
    // slots 5,6: builder, metadata
    std::vector<uint8_t> data(224, 0);
    data[31] = 0; // side = BUY
    data[63] = 0x01; // token id
    // slot 2: 600000 = 0x0927C0
    data[93] = 0x09; data[94] = 0x27; data[95] = 0xC0;
    // slot 3: 1000000 = 0x0F4240
    data[125] = 0x0F; data[126] = 0x42; data[127] = 0x40;
    // slot 4: 2000
    data[158] = 0x07; data[159] = 0xD0;

    RawLog log = make_log(CTF_EXCHANGE_V2, TOPIC0_ORDER_FILLED_V2, maker_addr, taker_addr, data);

    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];
    int count = decoder->decode(log, events, Decoder::MAX_EVENTS_PER_LOG);

    ASSERT_EQ(count, 2);
    // Maker is buying tokens
    EXPECT_EQ(events[0].kind, EventKind::FILL);
    EXPECT_EQ(events[0].side, FillSide::BUY);
    EXPECT_EQ(events[0].qty, 1000000);
    EXPECT_EQ(events[0].price, 600000);
    EXPECT_EQ(events[0].fee, 0);

    // Taker is selling tokens
    EXPECT_EQ(events[1].kind, EventKind::FILL);
    EXPECT_EQ(events[1].side, FillSide::SELL);
    EXPECT_EQ(events[1].qty, 1000000);
    EXPECT_EQ(events[1].price, 600000);
    EXPECT_EQ(events[1].fee, 2000);
}

// Test NegRisk flag on NegRisk Exchange
TEST_F(DecoderTest, NegRiskFlag_Set) {
    uint8_t maker[20] = {0x01}, taker[20] = {0x02};
    std::vector<uint8_t> data(224, 0);
    data[31] = 0;
    data[63] = 1;
    data[95] = 100;
    data[127] = 100;

    RawLog log = make_log(NEGRISK_EXCHANGE_V2, TOPIC0_ORDER_FILLED_V2, maker, taker, data);
    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];
    int count = decoder->decode(log, events, Decoder::MAX_EVENTS_PER_LOG);

    ASSERT_GE(count, 1);
    EXPECT_TRUE(events[0].flags & EventFlags::NEGRISK);
}

// Test malformed log bounds checking (never crashes, returns 0)
TEST_F(DecoderTest, MalformedData_ReturnsZero) {
    uint8_t maker[20] = {0x01}, taker[20] = {0x02};
    std::vector<uint8_t> data(10, 0xFF); // Truncated data!

    RawLog log = make_log(CTF_EXCHANGE_V2, TOPIC0_ORDER_FILLED_V2, maker, taker, data);
    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];
    int count = decoder->decode(log, events, Decoder::MAX_EVENTS_PER_LOG);

    EXPECT_EQ(count, 0);
}

// Test unknown contract is safely ignored
TEST_F(DecoderTest, UnknownContract_Ignored) {
    uint8_t random_contract[20] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t maker[20] = {0x01}, taker[20] = {0x02};
    std::vector<uint8_t> data(224, 0);

    RawLog log = make_log(random_contract, TOPIC0_ORDER_FILLED_V2, maker, taker, data);
    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];
    int count = decoder->decode(log, events, Decoder::MAX_EVENTS_PER_LOG);

    EXPECT_EQ(count, 0);
}

// Test TransferSingle between two user wallets
TEST_F(DecoderTest, TransferSingle_UserToUser) {
    uint8_t operator_addr[20] = {0x99};
    uint8_t from_addr[20]     = {0x11, 0x22};
    uint8_t to_addr[20]       = {0x33, 0x44};

    RawLog log{};
    log.block_number = 100;
    std::memcpy(log.address, CTF_CONTRACT, 20);
    std::memcpy(log.topics[0], TOPIC0_TRANSFER_SINGLE, 32);
    std::memcpy(log.topics[1] + 12, operator_addr, 20);
    std::memcpy(log.topics[2] + 12, from_addr, 20);
    std::memcpy(log.topics[3] + 12, to_addr, 20);
    log.num_topics = 4;

    // 2 data slots: slot 0 = id, slot 1 = value (500_000)
    std::vector<uint8_t> data(64, 0);
    data[31] = 0x05; // token id 5
    data[61] = 0x07; data[62] = 0xA1; data[63] = 0x20; // 500000
    log.data_len = 64;
    std::memcpy(log.data, data.data(), 64);

    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];
    int count = decoder->decode(log, events, Decoder::MAX_EVENTS_PER_LOG);

    ASSERT_EQ(count, 2);
    // TRANSFER_OUT for sender
    EXPECT_EQ(events[0].kind, EventKind::TRANSFER_OUT);
    EXPECT_EQ(events[0].qty, 500000);
    EXPECT_TRUE(events[0].flags & EventFlags::HAS_TRANSFERS);

    // TRANSFER_IN for receiver
    EXPECT_EQ(events[1].kind, EventKind::TRANSFER_IN);
    EXPECT_EQ(events[1].qty, 500000);
    EXPECT_EQ(events[1].price, 0); // zero cost basis
    EXPECT_TRUE(events[1].flags & EventFlags::HAS_TRANSFERS);
}

// Test TransferSingle from zero address (mint) is ignored
TEST_F(DecoderTest, TransferSingle_MintSkipped) {
    uint8_t zero_addr[20] = {0};
    uint8_t to_addr[20]   = {0x33, 0x44};

    RawLog log{};
    std::memcpy(log.address, CTF_CONTRACT, 20);
    std::memcpy(log.topics[0], TOPIC0_TRANSFER_SINGLE, 32);
    std::memcpy(log.topics[2] + 12, zero_addr, 20);
    std::memcpy(log.topics[3] + 12, to_addr, 20);
    log.num_topics = 4;
    std::vector<uint8_t> data(64, 0);
    data[63] = 100;
    log.data_len = 64;
    std::memcpy(log.data, data.data(), 64);

    NormalizedEvent events[Decoder::MAX_EVENTS_PER_LOG];
    int count = decoder->decode(log, events, Decoder::MAX_EVENTS_PER_LOG);

    EXPECT_EQ(count, 0);
}

} // namespace
