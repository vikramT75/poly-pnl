# Edge Cases

Documented policies for every known edge case. Each entry has a test in `tests/unit/` or `tests/property/`.

---

## EC-001: Fill Direction and Asset ID Semantics

**Problem:** In the CTF Exchange, the `OrderFilled` event has `makerAssetId` and `takerAssetId`. In prediction markets, outcome tokens have non-zero IDs; USDC (the collateral) has ID `0`. Determining who is "buying" vs "selling" outcome shares requires interpreting which side holds which asset.

**Rule:**
- If `takerAssetId == 0`: the taker is paying USDC and receiving outcome tokens. **Taker is buying.**
- If `makerAssetId == 0`: the maker is paying USDC and receiving outcome tokens. **Maker is buying.**
- The fill price for outcome tokens = `makerAmountFilled / takerAmountFilled` (if taker is buying) — derived from the exchange amounts.

**Test:** `tests/unit/decoder_test.cpp:FillDirectionTest`

---

## EC-002: Partial Fills

**Problem:** An order may be filled across multiple `OrderFilled` events. Each event represents an independent fill, with its own quantity and effective price.

**Policy:** Each `OrderFilled` event is processed as an independent buy or sell. The engine does not need to track order-level state (order IDs are not part of the position model). The average cost basis naturally handles multiple partial fills at different prices.

**Example:**
- Fill 1: Buy 100 shares @ $0.60 → avg = $0.60, size = 100
- Fill 2: Buy 50 shares @ $0.65 → avg = (100×0.60 + 50×0.65)/150 = $0.617, size = 150

**Test:** `tests/unit/engine_test.cpp:PartialFillAverageCost`

---

## EC-003: Market Resolution — Wallets That Never Redeem

**Problem:** When a market resolves, winning tokens pay $1.00 and losing tokens pay $0.00. Wallets that hold positions but never call `redeemPositions` on-chain have no on-chain REDEEM event. The engine must infer resolution.

**Policy:**
1. The metadata worker polls Gamma API for market resolution status.
2. When resolution is detected (and resolution block is confirmed), the engine synthesizes REDEEM events for all holders of that market's tokens.
3. The synthetic REDEEM uses the payout price from Gamma API (1_000_000 for winner, 0 for loser).
4. The synthetic event is applied to the position, moving unrealized PnL to realized.
5. The synthetic event is **not** written to ClickHouse (it has no on-chain tx_hash); a flag `synthetic=true` distinguishes it.

**Edge case:** A wallet may partially redeem on-chain (e.g., half their position). The remaining half gets a synthetic REDEEM.

**Test:** `tests/unit/engine_test.cpp:ResolutionSyntheticRedeem`

---

## EC-004: NegRisk Conversions

**Problem:** The NegRisk Adapter allows users to convert individual outcome tokens into a single "NegRisk" token (representing a bundle of complementary outcomes). This changes position balances without a standard buy/sell.

**Policy (initial, pending full verification of event semantics):**
- Conversion FROM individual outcomes TO NegRisk: treat as SELL of individual outcome tokens at $0.50 each, BUY of NegRisk token at equivalent cost basis.
- Conversion FROM NegRisk TO individual outcomes: reverse of the above.
- This approximation is documented and flagged with `has_negrisk_conversion = true`.

**Limitation:** Until OQ-002 (NegRisk event semantics) is fully resolved, this may introduce small inaccuracies. Any wallet with NegRisk conversions is called out in `VALIDATION.md`.

**Test:** `tests/unit/engine_test.cpp:NegRiskConversionRoundTrip`

---

## EC-005: ERC-1155 Transfers (Non-Trade)

**Problem:** Outcome tokens can be moved via raw ERC-1155 `TransferSingle` / `TransferBatch` events, e.g., gifting tokens to another wallet, moving between Safe wallets, etc. These have no fill price.

**Policy:**
- **Transfer IN:** Treat as a purchase at price 0 (zero cost basis). The position gains `qty` shares with zero contribution to avg_price. This is conservative: future sells will show 100% apparent gain.
- **Transfer OUT:** Treat as a sale at the current mark price (or at avg_price if no mark available). PnL is realized at the best-known price.
- **Flag:** `position.has_transfers = true` on any affected position. The API response includes this flag to warn consumers.
- **Filtering:** Transfers to/from the zero address (minting/burning) are excluded — these correspond to on-chain protocol operations, not user trades.
- **Transfers between the user's own wallets** cannot be detected without a full wallet-graph analysis; this is a known limitation (see `LIMITATIONS.md`).

**Test:** `tests/unit/engine_test.cpp:TransferInZeroCostBasis`, `TransferOutAtMark`

---

## EC-006: Trades Through Proxy/Safe Wallets

**Problem:** Many Polymarket users trade through Gnosis Safe smart contract wallets. The on-chain `msg.sender` for all trades is the Safe's address. The Polymarket Data API may surface the user's EOA instead.

**Policy:**
- PnL is tracked per **on-chain address** (the Safe address) in the engine.
- The metadata worker attempts to fetch a proxy→EOA mapping from the Polymarket Data API (if such an endpoint exists — see OQ-003).
- If a mapping is found: the API's `/wallet/{addr}` endpoint accepts both the Safe address and the EOA, resolving to the same position.
- If no mapping exists: document as a known limitation. Validation mismatches for proxy-wallet-heavy accounts are categorized separately in `VALIDATION.md`.

**Test:** `tests/unit/engine_test.cpp:ProxyWalletMapping`

---

## EC-007: Rounding Policy and Cent-Level Mismatches

**Problem:** Integer division truncates toward zero. Over thousands of trades, tiny rounding errors accumulate. The Polymarket Data API may use different rounding.

**Policy:**
- All division is C++ integer truncation (toward zero).
- The rounding error per operation is bounded at ±1 unit ($0.000001).
- Over 10,000 trades, maximum cumulative rounding error: ±$0.01.
- In `VALIDATION.md`, mismatches ≤ $0.01 are classified as "rounding" and not counted as errors.
- Mismatches > $0.01 trigger root-cause investigation.

**Test:** `tests/property/rounding_test.cpp:BoundedRoundingError`

---

## EC-008: Selling More Than Tracked Size (Incomplete History)

**Problem:** If the backfill window starts mid-history (e.g., 90 days ago), a wallet may have bought tokens before the window and try to sell them within the window. The engine sees a sell with no prior buy → size goes negative.

**Policy:**
- **Never silently produce a negative size.**
- When a sell would bring `size < 0`:
  1. Realize PnL on `min(qty, size)` shares at the fill price.
  2. Set `size = 0`.
  3. Set `position.incomplete_history = true`.
  4. Emit a warning metric: `engine_incomplete_history_events_total`.
- **API exposure:** The `/wallet/{addr}` response includes `incomplete_history: true` for any such position, so consumers know the numbers may be inaccurate.

**Test:** `tests/unit/engine_test.cpp:OversellClamping`

---

## EC-009: Fee Handling

**Problem:** The CTF Exchange charges fees on fills. The fee is paid by one side (usually the taker). How is this represented in the engine?

**Policy:**
- The `OrderFilled` event includes a `fee` field (or it can be derived from amounts — see OQ-006 for exact field layout).
- Fees are subtracted from `realized_pnl` immediately on the fill event: `realized_pnl -= fee`.
- Fees are in USDC units (same int64 scale).
- Split/merge/redeem operations have no fee.

**Test:** `tests/unit/engine_test.cpp:FeeSubtraction`

---

## EC-010: Rollback and Replay Determinism

**Problem:** Blockchain reorgs require rolling back engine state to a prior block and re-applying canonical events. The engine must produce byte-identical state before and after a rollback+replay cycle.

**Policy:**
- The engine maintains a **rollback journal** of the last 128 blocks (see ADR-007).
- Each journal entry stores the inverse delta (what to undo) for every position touched in that block.
- On rollback: apply inverse deltas in reverse order.
- On replay: apply canonical events forward.
- **Determinism test:** `tests/property/determinism_test.cpp:RollbackReplayIdentical`

**Test:** `tests/property/determinism_test.cpp` — property test with random event streams, random rollback points.
