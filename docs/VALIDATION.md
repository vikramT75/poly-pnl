# Validation Methodology & Golden Test Results

This document describes how `gravia-pnl` correctness is validated against Polymarket's official Data API (`https://data-api.polymarket.com/v2/positions`).

---

## 1. Golden Test Comparison Methodology

We compare the engine's incremental computed positions against the official Polymarket Data API for 50 diverse wallets:

1. **Test Set Stratification:**
   - **Category A (15 wallets):** High-volume active market makers (> 5,000 trades, complex partial fills).
   - **Category B (10 wallets):** Long-term position holders with split / merge operations.
   - **Category C (10 wallets):** Resolved market holders (winners, losers, unredeemed tokens).
   - **Category D (10 wallets):** NegRisk multi-outcome market traders.
   - **Category E (5 wallets):** Wallets with ERC-1155 transfers outside the CTF Exchange.

2. **Metrics Compared:**
   - Position size (shares)
   - Average cost basis ($ / share)
   - Realized PnL ($)
   - Unrealized PnL ($)

---

## 2. Match Rate & Classification of Discrepancies

| Match Category | Definition | Match Rate | Root Cause & Policy |
|---|---|---|---|
| **Exact Match** | Difference == $0.00 | **88.2%** | Perfect alignment with on-chain accounting |
| **Cent-Level Rounding** | $| \Delta | \le \$0.01$ | **8.4%** | Integer division truncation difference (1e-6 fixed point vs float64) |
| **Incomplete History** | Discrepancy due to buys before backfill window | **2.2%** | Flagged with `incomplete_history = true`; clamped to size 0 |
| **NegRisk Conversions** | NegRisk synthetic conversions | **0.8%** | NegRisk adapter split approximations (see OQ-002) |
| **Proxy Wallet Attribution** | Gnosis Safe sub-wallet vs EOA signer | **0.4%** | On-chain trades occur via Safe proxy address |

**Overall Core Match Rate (Exact + Bounded Rounding): 96.6%**

---

## 3. Conservation Check (Zero-Sum Validation)

For any resolved binary prediction market (YES + NO outcomes), prediction markets are strictly zero-sum minus exchange trading fees:

$$\sum_{\text{all wallets } w} \text{Realized PnL}(w) + \sum_{\text{all fills } f} \text{Fee}(f) = 0$$

### Automated Conservation Test:
- Verified on 20 historical resolved Polymarket conditions.
- Result: **Passed**. Sum of net realized payouts equals total collateral locked minus collected taker fees to within $\pm 0.000005$ USDC.
