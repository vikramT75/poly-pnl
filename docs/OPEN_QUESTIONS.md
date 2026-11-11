# Open Questions

Questions that cannot be answered without external verification. Each item blocks or risks a phase.
Agent must not guess — record here and research before coding the affected component.

---

## OQ-001: Verified Contract Addresses on Polygon Mainnet

**Blocking:** Phase 0, Phase 1 (decoder fixtures)

**Question:** What are the exact checksummed addresses for:
- CTF Exchange (primary Polymarket trading contract)
- NegRisk CTF Exchange
- Conditional Tokens Framework (CTF) contract
- NegRisk Adapter

**Status:** 🔍 Research in progress (research subagent running)

**Sources to check:** `docs.polymarket.com`, `github.com/Polymarket/ctf-exchange`, Polygonscan

**Placeholder (DO NOT USE in production until verified):**
- CTF Exchange: TBD
- NegRisk Exchange: TBD  
- CTF: TBD
- NegRisk Adapter: TBD

---

## OQ-002: NegRisk Event Semantics

**Blocking:** Phase 3 (accounting for NegRisk)

**Question:** What is the exact conversion math in NegRisk events?
- When a user converts individual outcome tokens to NegRisk tokens, what exactly happens to their position?
- What is the correct cost-basis treatment at the engine level?
- Are there events on the NegRisk Adapter that mirror PositionSplit/PositionsMerge, or are they distinct?

**Status:** 🔍 Needs research; will check Polymarket's NegRisk documentation and adapter source code.

---

## OQ-003: Proxy/Safe Wallet Resolution

**Blocking:** Phase 3 (validation accuracy)

**Question:** Does the Polymarket Data API expose a `proxy_wallet` ↔ `EOA` mapping? If so, what endpoint?
Many users trade through Gnosis Safe smart contract wallets. The on-chain `msg.sender` for trades is the Safe's address, but the Data API may surface trades under the user's EOA.

**Status:** 🔍 Needs research. If a mapping exists, the ingestor's metadata worker should fetch and cache it.

**Impact:** Mismatch in wallet-level validation if unresolved. Documented as known limitation if not resolvable.

---

## OQ-004: Envio HyperSync Free-Tier Rate Limits

**Blocking:** Phase 2 (backfill pipeline)

**Question:** What are the exact rate limits, request size limits, and block-range limits on Envio HyperSync's free tier for Polygon?

**Status:** 🔍 Check `docs.envio.dev` and HyperSync API docs.

**Contingency:** If limits are too restrictive, fall back to `eth_getLogs` with a public RPC (Alchemy free tier: 300M compute units/month).

---

## OQ-005: CLOB WebSocket Reconnect Behavior

**Blocking:** Phase 4 (live mode)

**Question:** When reconnecting to Polymarket's CLOB WebSocket after a disconnect:
- Does the server replay missed messages?
- Or does it only stream from the reconnect point forward?
- Is there a sequence number or cursor to request a replay window?

**Status:** 🔍 Check Polymarket CLOB API docs at `clob.polymarket.com` or `docs.polymarket.com`.

**Impact:** If no replay: on reconnect, the ingestor must fetch current order book snapshots via REST to re-seed marks. Engine marks may be stale for up to the reconnect latency.

---

## OQ-006: OrderFilled Event — Exact Field Layout

**Blocking:** Phase 1 (decoder)

**Question:** In the CTF Exchange `OrderFilled` event:
- Which fields are indexed (topics) vs non-indexed (data)?
- What is the exact ABI encoding of the `order` struct (if it's a struct parameter)?
- Is `fee` included in the event, or only as a separate event?
- Maker vs taker: which side holds which asset in a typical YES/NO prediction market fill?

**Status:** 🔍 Research subagent checking `github.com/Polymarket/ctf-exchange` ABI.

---

## OQ-007: Polymarket Data API Current Endpoints

**Blocking:** Phase 3 (golden tests)

**Question:** Confirm the current base URL and exact endpoints for:
- `/positions` — does it accept wallet address as a query param?
- `/trades` — same
- Pagination scheme (cursor vs offset?)
- Rate limits

The spec says "verify current endpoints from docs" — do not use guessed URLs.

**Status:** 🔍 Research subagent checking `docs.polymarket.com` and the Data API reference.

---

## OQ-008: Gamma API — Token/Condition Schema

**Blocking:** Phase 2 (metadata sync)

**Question:** What is the exact response schema for Gamma API market metadata?
- What field names map token IDs to condition IDs to outcome indices?
- How are resolution prices (payout numerators) exposed?
- What is the pagination scheme?

**Status:** 🔍 Research subagent checking `gamma-api.polymarket.com` docs.

---

## UPDATE LOG

| Date | OQ | Update |
|------|-----|--------|
| 2026-11-11 | All | Initial creation; research subagent dispatched |
