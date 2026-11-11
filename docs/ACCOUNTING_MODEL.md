# Accounting Model

This document specifies exactly how the PnL engine computes profit and loss.
Every formula here is implemented in `core/src/engine/pnl_engine.cpp` and tested in `tests/unit/`.

---

## 1. Representation

### Fixed-Point Integers

All monetary values use **int64 with scale 1,000,000** (six decimal places):

| Quantity | Unit | int64 value | Example |
|----------|------|-------------|---------|
| 1 share | 1_000_000 | 1000000 | Full share |
| $0.65 price | 650_000 | 650000 | 65-cent price |
| $1.00 | 1_000_000 | 1000000 | Dollar |
| $0.001 | 1_000 | 1000 | 0.1 cent |

USDC on Polygon has 6 decimal places. On-chain amounts (from event logs) are already in this scale (1 USDC = 1,000,000 on-chain units), so no scaling conversion is needed for price/collateral. Outcome token quantities from `OrderFilled` are also in 1e6 scale.

### Per-Position State

```cpp
struct Position {
    int64_t size;          // in 1e-6 shares (1 share = 1_000_000)
    int64_t avg_price;     // in 1e-6 USDC per share (1.00 = 1_000_000)
    int64_t realized_pnl;  // in 1e-6 USDC (cumulative)
    bool    incomplete_history; // true if window start truncated history
    bool    has_transfers;      // true if ERC-1155 transfers affected position
};
```

Unrealized PnL is computed on demand: `size × (mark - avg_price) / 1e6`.

---

## 2. Per-Event Rules

### 2.1 BUY Fill

```
new_size  = old_size + qty
avg_price = (old_size * old_avg + qty * fill_price) / new_size
size      = new_size
realized_pnl -= fee   // fee subtracted immediately
```

**Intermediate overflow protection:** `old_size * old_avg` can be up to ~9.2e18 (fits int64 marginally). To be safe, use `__int128` for the numerator:

```cpp
__int128 num = (__int128)old_size * old_avg + (__int128)qty * fill_price;
avg_price     = (int64_t)(num / new_size);
```

**Edge case:** If `old_size == 0`, avg_price = fill_price (no division).

### 2.2 SELL Fill

```
realized_pnl += qty * (fill_price - avg_price) / 1e6
size         -= qty
realized_pnl -= fee
```

If `qty > size` (incomplete history):
- Realize on `size` shares only
- Set `size = 0`, set `incomplete_history = true`
- Emit a warning metric

### 2.3 SPLIT (PositionSplit)

A split of `amount` USDC collateral into YES + NO tokens:

```
// Treat as buying 'amount' YES at $0.50 and 'amount' NO at $0.50
buy(wallet, yes_token_id, qty=amount, price=500_000)
buy(wallet, no_token_id,  qty=amount, price=500_000)
// No fee on splits
```

Where `price=500_000` = $0.50 in fixed-point. This is the fair value of a complementary pair.

### 2.4 MERGE (PositionsMerge)

A merge of `amount` YES + `amount` NO back into USDC:

```
// Treat as selling 'amount' YES at $0.50 and 'amount' NO at $0.50
sell(wallet, yes_token_id, qty=amount, price=500_000)
sell(wallet, no_token_id,  qty=amount, price=500_000)
// No fee on merges
```

### 2.5 REDEEM (PayoutRedemption)

Redemption of a resolved outcome token for payout:

```
payout_price = resolution_price_from_gamma_api  // 1_000_000 for winner, 0 for loser
sell(wallet, token_id, qty=position_size, price=payout_price)
// No additional fee
```

If the wallet holds a losing position and never redeems, the engine applies a synthetic REDEEM at market resolution time (detected via Gamma API resolution status polling).

### 2.6 ERC-1155 Transfer IN (non-trade)

Tokens received via raw ERC-1155 transfer (not from a trade):

```
// Conservative: treat as zero-cost acquisition
buy(wallet, token_id, qty=amount, price=0)
position.has_transfers = true
```

This is conservative: it means subsequent sells will show 100% gain. This is flagged in the API response.

**Alternative policy considered:** Use current mark as cost basis. Rejected because mark may not be available at transfer time and creates complexity.

### 2.7 ERC-1155 Transfer OUT (non-trade)

Tokens sent via raw ERC-1155 transfer (not from a trade):

```
// Treat as sell at current mark (best available price)
mark = current_mark_or_last_known(token_id)
sell(wallet, token_id, qty=amount, price=mark)
position.has_transfers = true
```

If no mark is available: use `avg_price` (zero realized PnL on this leg).

### 2.8 Market Resolution (synthetic)

When Gamma API reports a market resolved:

```
for each (wallet, token_id) in market_positions:
    if position.size > 0 and not already_redeemed:
        payout = resolution_payout(token_id)   // 1_000_000 or 0
        apply_redeem(wallet, token_id, position.size, payout)
```

Applied lazily on the resolution block detection, not retroactively.

---

## 3. Unrealized PnL

Computed on-demand (not stored):

```
unrealized_pnl = size * (mark - avg_price) / 1_000_000
```

Where `mark` is the latest CLOB midpoint price for the token.

For the **live holder-index update** (efficiency):

```
// When mark changes by delta = new_mark - old_mark:
for each wallet in token_holders:
    wallet.running_unrealized += wallet.position_size * delta / 1_000_000
```

This avoids a full position recalculation per tick. Batched in ~50-100 ms windows.

---

## 4. Overflow Analysis

| Operation | Max value | Fits int64? |
|-----------|-----------|-------------|
| `size` | ~9.2T shares (9.2e18 units) | Yes (INT64_MAX = 9.22e18) |
| `avg_price` | 1_000_000 (=$1.00) | Yes |
| `size * avg_price` (intermediate) | 9.2e18 × 1e6 = 9.2e24 | **No — use __int128** |
| `qty * price` (intermediate) | Same | **No — use __int128** |
| `realized_pnl` | Cumulative; could reach ±1e15 USDC theoretically | Yes (practical max <<< INT64_MAX) |

**Rule:** Wherever two int64 values scaled to 1e6 are multiplied together, use `__int128` for the intermediate result. The final divided result fits int64.

---

## 5. Rounding Policy

All divisions use **truncation toward zero** (C++ default for integer division).

**Effect:** Rounding errors are bounded at ±1 unit (=$0.000001 = 0.0001 cent) per operation.

**Cumulative effect over N trades:** bounded at ±N units. For 10,000 trades, max cumulative rounding error is ±$0.01.

**Validation:** The `docs/VALIDATION.md` golden-test results report the distribution of mismatches attributable to rounding vs other causes.

---

## 6. Win Rate Definition

For the leaderboard "win rate" metric:

```
win_rate = winning_markets / (winning_markets + losing_markets)
```

Where:
- **winning_market:** any resolved market where the wallet's realized_pnl from that market > 0
- **losing_market:** any resolved market where the wallet's realized_pnl from that market < 0
- Unresolved markets and break-even markets are excluded from the denominator

This is documented here so it can be explained precisely. The API exposes this definition via `GET /wallet/{addr}`.
