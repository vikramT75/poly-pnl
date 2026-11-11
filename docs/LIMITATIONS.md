# Limitations

Honest documentation of known inaccuracies, unsupported cases, and scale caveats.
This section is required reading before interpreting any output from gravia-pnl.

---

## L-001: Backfill Window Truncation

**Impact:** Cost basis inaccuracy for wallets with trading history older than the backfill window.

**Detail:** The default backfill window is 90 days. Any wallet that bought tokens before this window will have no recorded purchase event. When those tokens are sold within the window, the engine sets `incomplete_history = true` on the position, clamps size to zero, and excludes the position from unrealized PnL.

**Consequence:** Realized PnL for such positions is understated (the sell appears as a gain from zero basis). The `incomplete_history` flag in the API response indicates this.

**Mitigation:** Increase the backfill window (configurable via `BACKFILL_DAYS` env var). Full history requires ingesting from the CTF Exchange genesis block (~2021) which may take hours and significant storage.

---

## L-002: NegRisk Approximation

**Impact:** Small PnL inaccuracy for wallets that use NegRisk conversions.

**Detail:** NegRisk conversions are treated as split/merge equivalents at $0.50 cost basis. The exact conversion math may differ (see OQ-002). Affected positions have `has_negrisk_conversion = true`.

**Mitigation:** Once OQ-002 is fully resolved, the conversion handler will be updated and all affected positions recalculated.

---

## L-003: Proxy/Safe Wallet Attribution

**Impact:** Wallet-level PnL may differ from what Polymarket's UI shows for some users.

**Detail:** Some users trade through Gnosis Safe smart contract wallets. PnL is tracked per on-chain Safe address. If the Polymarket Data API exposes trades under the user's EOA, validation comparisons for such accounts will show discrepancies.

**Mitigation:** The metadata worker attempts to resolve proxy-to-EOA mappings via the Data API. If a mapping exists, both addresses are accepted. If not resolvable, documented separately in `VALIDATION.md`.

---

## L-004: ERC-1155 Transfer Edge Cases

**Impact:** PnL inaccuracy for positions affected by raw token transfers.

**Detail:** Tokens received via raw ERC-1155 transfer are assigned zero cost basis (conservative). Tokens sent are treated as sold at the last-known mark. Transfers between a user's own wallets appear as independent sell/buy events.

**Flag:** `has_transfers = true` on affected positions in API responses.

---

## L-005: Resolution Detection Lag

**Impact:** Up to `GAMMA_POLL_INTERVAL` seconds of lag (default: 60 seconds) between market resolution on-chain and the engine applying synthetic REDEEM events.

**Detail:** Market resolution is detected via polling the Gamma API, not via on-chain events. During the lag, affected positions show stale unrealized PnL.

**Mitigation:** Could monitor resolution-related on-chain events directly (future work). Current approach is simpler and sufficient for the demo.

---

## L-006: Mark Price Accuracy

**Impact:** Unrealized PnL accuracy depends on CLOB mark quality.

**Detail:** Marks are CLOB midpoint prices from the Polymarket WebSocket. For illiquid markets, the spread may be wide and the midpoint unrepresentative of actual execution price. Last-trade price is an alternative (see CLOB docs).

**Mitigation:** Mark source (midpoint vs last trade) is configurable. Default is midpoint to match Polymarket's UI.

---

## L-007: Scale Ceiling

**Impact:** Memory usage grows linearly with tracked positions.

**Detail:** Current implementation targets ~10M wallets × average 5 positions = 50M positions in RAM. Each position is ~50 bytes → ~2.5 GB for state alone. Beyond this, would require distributed sharding across multiple machines.

**Current headroom:** A single modern server with 32 GB RAM can handle the full Polymarket user base as of late 2024 (~2M active wallets). The system is designed to scale to 10M as a safety margin.

---

## L-008: No Order Placement or Execution

**By design.** This is a read-only PnL tracking system. No trade execution, no order management.

---

## L-009: Rounding Errors (Bounded)

**Impact:** PnL values may differ from Polymarket's UI by up to ±$0.01 per 10,000 trades due to integer truncation rounding.

**Detail:** See `ACCOUNTING_MODEL.md` section 5. Rounding is bounded and predictable. The validation suite classifies mismatches ≤ $0.01 as "rounding" rather than errors.

---

## L-010: CI/CD and Public Deployment

**Detail:** The public demo runs on a small VPS (2 vCPU, 4 GB RAM) with the 90-day backfill window. Numbers from this deployment are clearly labeled "demo scale" in the README. Production-scale benchmarks are run on a dedicated benchmark machine (see `bench/SETUP.md`).
