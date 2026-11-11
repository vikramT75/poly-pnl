#include "decoder.h"
#include <cstring>

// ============================================================================
// Decoder implementation — hand-written ABI decoding for Polymarket events
//
// Design rules (enforced):
//   1. No exceptions, no RTTI
//   2. No heap allocation — all locals are stack or passed-in buffers
//   3. Every data access is bounds-checked; malformed input returns 0 events
//   4. libFuzzer harness is in tests/fuzz/decoder_fuzz.cpp
//
// V1/V2 awareness: See docs/CONTRACTS_AND_APIS.md for the full upgrade story.
// ============================================================================

namespace gravia {
namespace decoder {

// ---------------------------------------------------------------------------
// Main dispatch
// ---------------------------------------------------------------------------

int Decoder::decode(const RawLog& log,
                    NormalizedEvent* out,
                    int out_capacity) const noexcept
{
    if (!out || out_capacity < 1) return 0;
    if (log.num_topics < 1) return 0;

    const uint8_t* t0 = log.topics[0];
    const uint8_t* addr = log.address;

    // --- CTF Exchange V1 / NegRisk V1: OrderFilled V1 ---
    if (topic_eq(t0, TOPIC0_ORDER_FILLED_V1)) {
        bool negrisk = addr_eq(addr, NEGRISK_EXCHANGE_V1);
        if (addr_eq(addr, CTF_EXCHANGE_V1) || negrisk) {
            return decode_order_filled_v1(log, out, out_capacity, negrisk);
        }
        return 0; // unknown contract with this topic; ignore
    }

    // --- CTF Exchange V2 / NegRisk V2: OrderFilled V2 ---
    if (topic_eq(t0, TOPIC0_ORDER_FILLED_V2)) {
        bool negrisk = addr_eq(addr, NEGRISK_EXCHANGE_V2);
        if (addr_eq(addr, CTF_EXCHANGE_V2) || negrisk) {
            return decode_order_filled_v2(log, out, out_capacity, negrisk);
        }
        return 0;
    }

    // --- CTF (Gnosis) events ---
    if (addr_eq(addr, CTF_CONTRACT)) {
        if (topic_eq(t0, TOPIC0_POSITION_SPLIT))
            return decode_position_split(log, out);
        if (topic_eq(t0, TOPIC0_POSITIONS_MERGE))
            return decode_positions_merge(log, out);
        if (topic_eq(t0, TOPIC0_PAYOUT_REDEMPTION))
            return decode_payout_redemption(log, out);
        if (topic_eq(t0, TOPIC0_TRANSFER_SINGLE))
            return decode_transfer_single(log, out);
        if (topic_eq(t0, TOPIC0_TRANSFER_BATCH))
            return decode_transfer_batch(log, out, out_capacity);
        return 0;
    }

    // --- NegRisk Adapter events ---
    if (addr_eq(addr, NEGRISK_ADAPTER)) {
        if (topic_eq(t0, TOPIC0_POSITIONS_CONVERTED))
            return decode_positions_converted(log, out);
        return 0; // MarketPrepared, QuestionPrepared: not relevant to PnL
    }

    return 0; // unrecognized contract
}

// ---------------------------------------------------------------------------
// OrderFilled V1 decoding
//
// Topics: [topic0][orderHash][maker][taker]
// Data:   [makerAssetId][takerAssetId][makerAmountFilled][takerAmountFilled][fee]
//
// Direction: If makerAssetId == 0, maker is sending collateral (buying tokens).
//            If takerAssetId == 0, taker is sending collateral (buying tokens).
//            One side must be 0 (collateral). We emit one event per side.
// ---------------------------------------------------------------------------
int Decoder::decode_order_filled_v1(const RawLog& log, NormalizedEvent* out, int out_capacity, bool negrisk) const noexcept {
    // Require 4 topics (topic0 + 3 indexed)
    if (log.num_topics < 4) return 0;
    // Require at least 5 data slots (5 × 32 = 160 bytes)
    if (log.data_len < 160) return 0;

    const uint8_t* data = log.data;
    const uint32_t dlen = log.data_len;

    // Read indexed fields from topics
    // topics[1] = orderHash (bytes32) — not needed for PnL
    uint8_t maker_addr[20], taker_addr[20];
    // topics[2] = maker address (left-padded to 32 bytes)
    std::memcpy(maker_addr, log.topics[2] + 12, 20);
    // topics[3] = taker address
    std::memcpy(taker_addr, log.topics[3] + 12, 20);

    // Read non-indexed data fields
    int64_t maker_asset_id_raw = abi_read_i64(data, dlen,   0); // slot 0
    int64_t taker_asset_id_raw = abi_read_i64(data, dlen,  32); // slot 1
    int64_t maker_amount       = abi_read_i64(data, dlen,  64); // slot 2
    int64_t taker_amount       = abi_read_i64(data, dlen,  96); // slot 3
    int64_t fee                = abi_read_i64(data, dlen, 128); // slot 4

    if (maker_asset_id_raw < 0 || taker_asset_id_raw < 0 ||
        maker_amount < 0 || taker_amount < 0 || fee < 0) return 0;
    if (maker_amount == 0 || taker_amount == 0) return 0;

    // Determine fill direction:
    // makerAssetId == 0 means maker holds collateral → maker BUYs outcome tokens
    // takerAssetId == 0 means taker holds collateral → taker BUYs outcome tokens
    // Exactly one of them should be 0 for a standard fill.

    bool maker_has_collateral = (maker_asset_id_raw == 0);
    bool taker_has_collateral = (taker_asset_id_raw == 0);

    if (!maker_has_collateral && !taker_has_collateral) return 0; // malformed
    if (maker_has_collateral && taker_has_collateral) return 0;   // malformed

    // The outcome token is on the non-collateral side
    // Build a 32-byte token ID from the raw uint256 value
    uint8_t outcome_token_id[32] = {};
    int64_t outcome_amount = 0;
    int64_t collateral_amount = 0;

    if (maker_has_collateral) {
        // Maker BUYs: maker sent collateral, received outcome tokens
        // takerAssetId is the outcome token
        // We need the full 32-byte taker asset id — read from raw data slot 1
        if (!abi_read_b32(data, dlen, 32, outcome_token_id)) return 0;
        collateral_amount = maker_amount;
        outcome_amount    = taker_amount;
    } else {
        // Taker BUYs: taker sent collateral, received outcome tokens
        // makerAssetId is the outcome token
        if (!abi_read_b32(data, dlen, 0, outcome_token_id)) return 0;
        collateral_amount = taker_amount;
        outcome_amount    = maker_amount;
    }

    if (outcome_amount == 0) return 0;

    // Price = collateral paid / outcome tokens received (in 1e6 scale already)
    // Both amounts are already in 6-decimal scale on-chain
    // price = collateral_amount * 1_000_000 / outcome_amount
    // But collateral is already 1e6-scaled, outcome too — so price in [0, 1_000_000]
    int64_t price = ((__int128)collateral_amount * 1'000'000LL) / outcome_amount;

    // Intern addresses and token
    WalletIdx maker_idx = intern_wallet(maker_addr);
    WalletIdx taker_idx = intern_wallet(taker_addr);
    TokenIdx  token_idx = intern_token(outcome_token_id);

    if (maker_idx == INVALID_WALLET || taker_idx == INVALID_WALLET || token_idx == INVALID_TOKEN)
        return 0;

    int n = 0;

    // Emit event for maker
    {
        NormalizedEvent& ev = out[n++];
        init_event(ev, log, EventKind::FILL);
        ev.wallet_idx = maker_idx;
        ev.token_idx  = token_idx;
        ev.qty        = outcome_amount;
        ev.price      = price;
        ev.fee        = static_cast<int32_t>(fee); // fee paid by taker in V1 by convention
        ev.side       = maker_has_collateral ? FillSide::BUY : FillSide::SELL;
        ev.flags     |= EventFlags::MAKER;
        if (negrisk) ev.flags |= EventFlags::NEGRISK;
        // fee is taker-side in V1; maker pays 0 fee (unless maker=taker, impossible)
        if (maker_has_collateral) ev.fee = 0;
        else ev.fee = static_cast<int32_t>(fee);
    }

    // Emit event for taker (if capacity allows)
    if (n < out_capacity) {
        NormalizedEvent& ev = out[n++];
        init_event(ev, log, EventKind::FILL);
        ev.wallet_idx = taker_idx;
        ev.token_idx  = token_idx;
        ev.qty        = outcome_amount;
        ev.price      = price;
        ev.side       = taker_has_collateral ? FillSide::BUY : FillSide::SELL;
        ev.flags      = negrisk ? EventFlags::NEGRISK : 0;
        if (taker_has_collateral) ev.fee = 0;
        else ev.fee = static_cast<int32_t>(fee);
    }

    return n;
}

// ---------------------------------------------------------------------------
// OrderFilled V2 decoding
//
// Topics: [topic0][orderHash][maker][taker]
// Data:   [side:uint8][tokenId:uint256][makerAmt][takerAmt][fee][builder][metadata]
//
// side=0 (BUY): maker sends collateral, receives tokenId. makerAmt=collateral, takerAmt=tokens.
// side=1 (SELL): maker sends tokenId, receives collateral. makerAmt=tokens, takerAmt=collateral.
// ---------------------------------------------------------------------------
int Decoder::decode_order_filled_v2(const RawLog& log, NormalizedEvent* out, int out_capacity, bool negrisk) const noexcept {
    if (log.num_topics < 4) return 0;
    if (log.data_len < 7 * 32) return 0; // 7 slots minimum

    const uint8_t* data = log.data;
    const uint32_t dlen = log.data_len;

    uint8_t maker_addr[20], taker_addr[20];
    std::memcpy(maker_addr, log.topics[2] + 12, 20);
    std::memcpy(taker_addr, log.topics[3] + 12, 20);

    // slot 0: side (uint8, right-padded in 32-byte slot → read from byte 31)
    uint8_t side_val = (dlen >= 32) ? data[31] : 0xFF;
    if (side_val > 1) return 0; // invalid side

    // slot 1: tokenId (full 32 bytes)
    uint8_t token_id[32];
    if (!abi_read_b32(data, dlen, 32, token_id)) return 0;

    // slot 2: makerAmountFilled
    int64_t maker_amount = abi_read_i64(data, dlen, 64);
    // slot 3: takerAmountFilled
    int64_t taker_amount = abi_read_i64(data, dlen, 96);
    // slot 4: fee
    int64_t fee = abi_read_i64(data, dlen, 128);
    // slots 5,6: builder, metadata — not needed for PnL

    if (maker_amount < 0 || taker_amount < 0 || fee < 0) return 0;
    if (maker_amount == 0 || taker_amount == 0) return 0;

    // side=0 (BUY): maker sends collateral (makerAmt), receives tokens (takerAmt)
    // side=1 (SELL): maker sends tokens (makerAmt), receives collateral (takerAmt)
    int64_t collateral_amount = (side_val == 0) ? maker_amount : taker_amount;
    int64_t token_amount      = (side_val == 0) ? taker_amount : maker_amount;

    if (token_amount == 0) return 0;

    int64_t price = ((__int128)collateral_amount * 1'000'000LL) / token_amount;

    WalletIdx maker_idx = intern_wallet(maker_addr);
    WalletIdx taker_idx = intern_wallet(taker_addr);
    TokenIdx  tok_idx   = intern_token(token_id);

    if (maker_idx == INVALID_WALLET || taker_idx == INVALID_WALLET || tok_idx == INVALID_TOKEN)
        return 0;

    // In V2, the fee is paid by the taker. maker_fee = 0, taker_fee = fee.
    FillSide maker_side = (side_val == 0) ? FillSide::BUY : FillSide::SELL;
    FillSide taker_side = (side_val == 0) ? FillSide::SELL : FillSide::BUY;

    int n = 0;

    {
        NormalizedEvent& ev = out[n++];
        init_event(ev, log, EventKind::FILL);
        ev.wallet_idx = maker_idx;
        ev.token_idx  = tok_idx;
        ev.qty        = token_amount;
        ev.price      = price;
        ev.side       = maker_side;
        ev.fee        = 0; // maker pays no fee in V2
        ev.flags      = EventFlags::MAKER | (negrisk ? EventFlags::NEGRISK : 0);
    }

    if (n < out_capacity) {
        NormalizedEvent& ev = out[n++];
        init_event(ev, log, EventKind::FILL);
        ev.wallet_idx = taker_idx;
        ev.token_idx  = tok_idx;
        ev.qty        = token_amount;
        ev.price      = price;
        ev.side       = taker_side;
        ev.fee        = static_cast<int32_t>(fee);
        ev.flags      = negrisk ? EventFlags::NEGRISK : 0;
    }

    return n;
}

// ---------------------------------------------------------------------------
// PositionSplit decoding
//
// Topics: [topic0][stakeholder][parentCollectionId][conditionId]
// Data:   [collateralToken addr (slot 0)][partition[] offset (slot 1)]...[amount (last slot)]
//
// For binary markets (YES/NO), partition = [1, 2], producing 2 outcome tokens.
// We need the conditionId from topics[3] to look up YES/NO token IDs via metadata.
// For now, emit a SPLIT event with conditionId — engine resolves to token IDs via metadata.
//
// Simplified approach: emit a single SPLIT event carrying the conditionId as token_idx
// placeholder. The engine looks up YES/NO token IDs from the metadata table.
// ---------------------------------------------------------------------------
int Decoder::decode_position_split(const RawLog& log, NormalizedEvent* out) const noexcept {
    if (log.num_topics < 4) return 0;
    // We need at least: collateral addr (slot 0), partition offset (slot 1), amount.
    // For binary markets, partition array has 2 elements → data = [addr][offset][len=2][1][2][amount]
    // Minimum data length: 6 × 32 = 192 bytes
    if (log.data_len < 192) return 0;

    const uint8_t* data = log.data;
    const uint32_t dlen = log.data_len;

    // stakeholder from topics[1]
    uint8_t stakeholder[20];
    std::memcpy(stakeholder, log.topics[1] + 12, 20);

    // conditionId from topics[3] (full 32 bytes, used as a "token group" key)
    uint8_t condition_id[32];
    std::memcpy(condition_id, log.topics[3], 32);

    // amount: last static slot. With partition=[1,2], data layout:
    // slot 0: collateralToken (addr, 32 bytes)
    // slot 1: offset to partition array = 0x40 = 64
    // slot 2: partition.length = 2
    // slot 3: partition[0] = 1
    // slot 4: partition[1] = 2
    // slot 5: amount
    // For dynamic arrays, we follow the offset. Offset at slot 1 = 64 (points to slot 2).
    // The amount is at the last slot: slot 0 (32) + dynamic section.
    // Parse the partition offset to find amount correctly.
    uint32_t partition_offset = abi_read_u32(data, dlen, 32); // slot 1 low 4 bytes
    if (partition_offset >= dlen) return 0;
    uint32_t partition_len_offset = partition_offset; // partition_offset is from slot 0
    int64_t partition_len = abi_read_i64(data, dlen, partition_len_offset);
    if (partition_len <= 0 || partition_len > 64) return 0; // sanity check
    uint32_t amount_offset = partition_len_offset + 32 + (uint32_t)partition_len * 32;
    int64_t amount = abi_read_i64(data, dlen, amount_offset);
    if (amount <= 0) return 0;

    WalletIdx wallet_idx = intern_wallet(stakeholder);
    // Use conditionId bytes as a "group token id" key into intern table
    // The engine maps this to YES/NO via metadata; for SPLIT events token_idx = condition group
    TokenIdx cond_idx = intern_token(condition_id);

    if (wallet_idx == INVALID_WALLET || cond_idx == INVALID_TOKEN) return 0;

    NormalizedEvent& ev = out[0];
    init_event(ev, log, EventKind::SPLIT);
    ev.wallet_idx = wallet_idx;
    ev.token_idx  = cond_idx;   // conditionId used as group key; engine resolves YES/NO
    ev.qty        = amount;     // collateral amount split
    ev.price      = 500'000;    // $0.50 cost basis per side
    return 1;
}

// ---------------------------------------------------------------------------
// PositionsMerge decoding — same layout as PositionSplit
// ---------------------------------------------------------------------------
int Decoder::decode_positions_merge(const RawLog& log, NormalizedEvent* out) const noexcept {
    if (log.num_topics < 4) return 0;
    if (log.data_len < 192) return 0;

    const uint8_t* data = log.data;
    const uint32_t dlen = log.data_len;

    uint8_t stakeholder[20];
    std::memcpy(stakeholder, log.topics[1] + 12, 20);

    uint8_t condition_id[32];
    std::memcpy(condition_id, log.topics[3], 32);

    uint32_t partition_offset = abi_read_u32(data, dlen, 32);
    if (partition_offset >= dlen) return 0;
    int64_t partition_len = abi_read_i64(data, dlen, partition_offset);
    if (partition_len <= 0 || partition_len > 64) return 0;
    uint32_t amount_offset = partition_offset + 32 + (uint32_t)partition_len * 32;
    int64_t amount = abi_read_i64(data, dlen, amount_offset);
    if (amount <= 0) return 0;

    WalletIdx wallet_idx = intern_wallet(stakeholder);
    TokenIdx  cond_idx   = intern_token(condition_id);
    if (wallet_idx == INVALID_WALLET || cond_idx == INVALID_TOKEN) return 0;

    NormalizedEvent& ev = out[0];
    init_event(ev, log, EventKind::MERGE);
    ev.wallet_idx = wallet_idx;
    ev.token_idx  = cond_idx;
    ev.qty        = amount;
    ev.price      = 500'000;
    return 1;
}

// ---------------------------------------------------------------------------
// PayoutRedemption decoding
//
// Topics: [topic0][redeemer][collateralToken][parentCollectionId]
// Data:   [conditionId (slot 0)][indexSets offset (slot 1)][... dynamic ...][payout (last)]
// ---------------------------------------------------------------------------
int Decoder::decode_payout_redemption(const RawLog& log, NormalizedEvent* out) const noexcept {
    if (log.num_topics < 4) return 0;
    if (log.data_len < 128) return 0;

    const uint8_t* data = log.data;
    const uint32_t dlen = log.data_len;

    uint8_t redeemer[20];
    std::memcpy(redeemer, log.topics[1] + 12, 20);

    uint8_t condition_id[32];
    if (!abi_read_b32(data, dlen, 0, condition_id)) return 0;

    // Parse indexSets to find payout slot
    uint32_t indexsets_offset = abi_read_u32(data, dlen, 32);
    if (indexsets_offset >= dlen) return 0;
    int64_t indexsets_len = abi_read_i64(data, dlen, indexsets_offset);
    if (indexsets_len < 0 || indexsets_len > 64) return 0;
    uint32_t payout_offset = indexsets_offset + 32 + (uint32_t)indexsets_len * 32;
    int64_t payout = abi_read_i64(data, dlen, payout_offset);
    if (payout < 0) return 0;

    WalletIdx wallet_idx = intern_wallet(redeemer);
    TokenIdx  cond_idx   = intern_token(condition_id);
    if (wallet_idx == INVALID_WALLET || cond_idx == INVALID_TOKEN) return 0;

    NormalizedEvent& ev = out[0];
    init_event(ev, log, EventKind::REDEEM);
    ev.wallet_idx    = wallet_idx;
    ev.token_idx     = cond_idx;
    ev.qty           = payout;    // collateral returned
    ev.payout_price  = static_cast<int32_t>(payout > 0 ? 1'000'000 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// TransferSingle decoding
//
// Topics: [topic0][operator][from][to]
// Data:   [id (slot 0)][value (slot 1)]
//
// Skip: mint (from=0), burn (to=0), and exchange contracts (they emit fills separately).
// ---------------------------------------------------------------------------
int Decoder::decode_transfer_single(const RawLog& log, NormalizedEvent* out) const noexcept {
    if (log.num_topics < 4) return 0;
    if (log.data_len < 64) return 0;

    const uint8_t* data = log.data;
    const uint32_t dlen = log.data_len;

    uint8_t from_addr[20], to_addr[20];
    std::memcpy(from_addr, log.topics[2] + 12, 20);
    std::memcpy(to_addr,   log.topics[3] + 12, 20);

    // Skip mints and burns
    if (addr_is_zero(from_addr) || addr_is_zero(to_addr)) return 0;

    // Skip if from/to is an exchange contract (those fills are covered by OrderFilled)
    static const uint8_t* exchanges[] = {
        CTF_EXCHANGE_V1, NEGRISK_EXCHANGE_V1,
        CTF_EXCHANGE_V2, NEGRISK_EXCHANGE_V2,
        NEGRISK_ADAPTER
    };
    for (auto* ex : exchanges) {
        if (addr_eq(from_addr, ex) || addr_eq(to_addr, ex)) return 0;
    }

    uint8_t token_id[32];
    if (!abi_read_b32(data, dlen, 0, token_id)) return 0;
    int64_t value = abi_read_i64(data, dlen, 32);
    if (value <= 0) return 0;

    WalletIdx from_idx  = intern_wallet(from_addr);
    WalletIdx to_idx    = intern_wallet(to_addr);
    TokenIdx  token_idx = intern_token(token_id);

    if (from_idx == INVALID_WALLET || to_idx == INVALID_WALLET || token_idx == INVALID_TOKEN)
        return 0;

    int n = 0;

    // TRANSFER_OUT for the sender
    {
        NormalizedEvent& ev = out[n++];
        init_event(ev, log, EventKind::TRANSFER_OUT);
        ev.wallet_idx = from_idx;
        ev.token_idx  = token_idx;
        ev.qty        = value;
        ev.flags      = EventFlags::HAS_TRANSFERS;
    }

    // TRANSFER_IN for the receiver
    if (n < 2) { // out must have capacity >= 2 for TransferSingle
        NormalizedEvent& ev = out[n++];
        init_event(ev, log, EventKind::TRANSFER_IN);
        ev.wallet_idx = to_idx;
        ev.token_idx  = token_idx;
        ev.qty        = value;
        ev.price      = 0; // zero cost basis
        ev.flags      = EventFlags::HAS_TRANSFERS;
    }

    return n;
}

// ---------------------------------------------------------------------------
// TransferBatch decoding
//
// Topics: [topic0][operator][from][to]
// Data:   [ids_offset (slot 0)][values_offset (slot 1)][ids array][values array]
// ---------------------------------------------------------------------------
int Decoder::decode_transfer_batch(const RawLog& log, NormalizedEvent* out, int out_cap) const noexcept {
    if (log.num_topics < 4) return 0;
    if (log.data_len < 64) return 0;

    const uint8_t* data = log.data;
    const uint32_t dlen = log.data_len;

    uint8_t from_addr[20], to_addr[20];
    std::memcpy(from_addr, log.topics[2] + 12, 20);
    std::memcpy(to_addr,   log.topics[3] + 12, 20);

    if (addr_is_zero(from_addr) || addr_is_zero(to_addr)) return 0;

    // Parse ids array
    uint32_t ids_offset    = abi_read_u32(data, dlen, 0);
    uint32_t values_offset = abi_read_u32(data, dlen, 32);
    if (ids_offset >= dlen || values_offset >= dlen) return 0;

    int64_t ids_len = abi_read_i64(data, dlen, ids_offset);
    int64_t vals_len = abi_read_i64(data, dlen, values_offset);
    if (ids_len != vals_len || ids_len <= 0 || ids_len > 64) return 0;

    WalletIdx from_idx = intern_wallet(from_addr);
    WalletIdx to_idx   = intern_wallet(to_addr);
    if (from_idx == INVALID_WALLET || to_idx == INVALID_WALLET) return 0;

    int n = 0;
    for (int64_t i = 0; i < ids_len && n + 1 < out_cap; ++i) {
        uint8_t token_id[32];
        uint32_t id_slot  = ids_offset    + 32 + (uint32_t)i * 32;
        uint32_t val_slot = values_offset + 32 + (uint32_t)i * 32;

        if (!abi_read_b32(data, dlen, id_slot, token_id)) break;
        int64_t value = abi_read_i64(data, dlen, val_slot);
        if (value <= 0) continue;

        TokenIdx tok_idx = intern_token(token_id);
        if (tok_idx == INVALID_TOKEN) continue;

        // TRANSFER_OUT
        {
            NormalizedEvent& ev = out[n++];
            init_event(ev, log, EventKind::TRANSFER_OUT);
            ev.wallet_idx = from_idx;
            ev.token_idx  = tok_idx;
            ev.qty        = value;
            ev.flags      = EventFlags::HAS_TRANSFERS;
        }
        // TRANSFER_IN
        if (n < out_cap) {
            NormalizedEvent& ev = out[n++];
            init_event(ev, log, EventKind::TRANSFER_IN);
            ev.wallet_idx = to_idx;
            ev.token_idx  = tok_idx;
            ev.qty        = value;
            ev.price      = 0;
            ev.flags      = EventFlags::HAS_TRANSFERS;
        }
    }

    return n;
}

// ---------------------------------------------------------------------------
// PositionsConverted decoding (NegRisk Adapter)
//
// Topics: [topic0][stakeholder][marketId][indexSet]
// Data:   [amount (slot 0)]
// ---------------------------------------------------------------------------
int Decoder::decode_positions_converted(const RawLog& log, NormalizedEvent* out) const noexcept {
    if (log.num_topics < 4) return 0;
    if (log.data_len < 32) return 0;

    uint8_t stakeholder[20];
    std::memcpy(stakeholder, log.topics[1] + 12, 20);

    // marketId used as a group key (like conditionId for SPLIT/MERGE)
    uint8_t market_id[32];
    std::memcpy(market_id, log.topics[2], 32);

    int64_t amount = abi_read_i64(log.data, log.data_len, 0);
    if (amount <= 0) return 0;

    WalletIdx wallet_idx = intern_wallet(stakeholder);
    TokenIdx  market_idx = intern_token(market_id);
    if (wallet_idx == INVALID_WALLET || market_idx == INVALID_TOKEN) return 0;

    // Treat as a SPLIT-equivalent (conservative per ADR-004)
    // Engine handles NegRisk-specific accounting via the NEGRISK flag
    NormalizedEvent& ev = out[0];
    init_event(ev, log, EventKind::SPLIT);
    ev.wallet_idx = wallet_idx;
    ev.token_idx  = market_idx;
    ev.qty        = amount;
    ev.price      = 500'000;
    ev.flags      = EventFlags::NEGRISK;
    return 1;
}

} // namespace decoder
} // namespace gravia
