# Architecture Decision Records (ADR)

This log records every significant design decision, the alternatives rejected, and the reasoning.
Maintained so the author can explain and defend every choice in an engineering interview.

---

## ADR-001: IPC Transport — Unix Domain Socket with length-prefixed binary frames

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** Use a Unix domain socket (UDS) with length-prefixed binary frames as the default transport between the Python ingestor and the C++ engine. A shared-memory SPSC ring buffer is documented as a Phase 5 optimization path.

**Alternatives rejected:**
- **TCP loopback:** Adds kernel networking overhead (even on localhost, TCP goes through the full stack); no benefit over UDS for local IPC.
- **Named pipe (Windows-only concept):** We target Linux; UDS is the idiomatic equivalent and more portable.
- **Shared-memory ring buffer (immediate):** Correct but significantly harder to implement, debug, and reason about initially. Lock-free shmem requires careful memory ordering, producer/consumer synchronization, and crash safety. Premature optimization before we have profiles.
- **gRPC/protobuf:** Too much overhead (serialization, framing, reflection) for a hot-path IPC link that only crosses process boundaries once.

**Why UDS:**
- Near-zero overhead: on Linux, UDS data transfer avoids TCP checksums and routing; `sendmsg`/`recvmsg` with a fixed-size header is fast.
- Debuggable with `strace`, `socat`, and standard tooling.
- If benchmarks show UDS is a bottleneck (profile first!), migrate to `mmap`-backed SPSC ring with a semaphore — the binary frame format is identical, only the transport changes.

**Frame format:**
```
[4 bytes: frame length (LE uint32)] [N bytes: RawLog binary frames or NormalizedEvents]
```

---

## ADR-002: API Server — Thin Python (FastAPI/uvicorn) Service

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** The HTTP/WebSocket API server is a thin Python service (FastAPI + uvicorn) that reads engine snapshots and queries the engine over a local socket, serving REST endpoints and live WebSocket updates to the frontend.

**Alternatives rejected:**
- **C++ Boost.Beast embedded in engine:** More C++ code to maintain; Beast's async model is verbose; pulls HTTP/WS concerns into the hot-path binary.
- **C++ Drogon:** Modern and fast, but adds an external C++ framework dependency.
- **Node.js/Express:** Adds another runtime environment to the stack when Python is already used for ingestion.

**Why FastAPI:**
- Leverages the same Python 3.12+ async runtime used for ingestion.
- Async WebSocket support via Starlette/uvicorn handles thousands of concurrent dashboard clients easily.
- Automatic OpenAPI/Swagger generation for easy testing and frontend integration.
- Fast JSON serialization via `orjson`.
- Simple integration with ClickHouse via `clickhouse-connect` for historical queries (PnL curves).

---

## ADR-009: Ingestion Engine — Python 3.12+ (asyncio, uvloop)

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** The ingestion layer is written in Python 3.12+ using `asyncio` with `uvloop`, the official `hypersync` Python client, `clickhouse-connect`, `websockets`/`aiohttp`, and `pydantic`.

**Alternatives rejected:**
- **Pure C++ ingestion:** Excessive boilerplate for HTTP/WebSocket handling, JSON parsing for RPC, and indexer client SDKs.
- **Go ingestion:** Replaced per engineering spec update to Python 3.12+ for indexer ecosystem synergy and rapid self-healing pipeline iteration.

**Architecture guardrails:**
- The ingestor acts strictly as an I/O-bound transport pipeline: fetch raw logs, handle reorgs/gaps, persist to ClickHouse, and stream raw log bytes over UDS to the C++ decoder.
- No heavy ABI decoding or position math in Python; the GIL is completely bypassed on the decoding and accounting hot path.
- Typed with `mypy --strict`, linted with `ruff`.

---

## ADR-003: Backfill Source — Envio HyperSync

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** Envio HyperSync is the primary backfill mechanism. Direct `eth_getLogs` via JSON-RPC is the fallback.

**Alternatives rejected:**
- **`eth_getLogs` loops only:** A free-tier Polygon RPC delivers ~5-50 blocks per request with a 10k-event cap per call. 90 days × ~43,200 blocks/day = ~3.9M blocks → thousands of requests → hours to days. Unusable for a demo that must start within minutes.
- **Goldsky / Subsquid:** Similar indexer capabilities but less documented free-tier access and more lock-in. HyperSync has an open SDK and clear Polygon support.
- **The Graph:** GraphQL indexer with its own schema; adds a schema-mapping layer and is slower for raw log delivery.

**Why HyperSync:**
- Delivers raw logs (not decoded) at up to 100k+ blocks/sec on fast connections.
- Supports filtering by contract address + topic hashes natively.
- Free tier covers our 90-day demo window.
- Falls back gracefully: if HyperSync is unavailable, the ingestor switches to `eth_getLogs` batching automatically.

**Rate limit handling:** HyperSync free tier limits documented in `docs/OPEN_QUESTIONS.md`; ingestor uses exponential backoff with jitter.

---

## ADR-004: Accounting Model — Average Cost Basis

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** Use average cost basis (ACB) for PnL calculation.

**Alternatives rejected:**
- **FIFO:** Requires tracking individual lot entries; much more memory per position; not what Polymarket's UI shows; harder to validate.
- **LIFO:** Same complexity as FIFO; tax-motivated; not relevant here.
- **Mark-to-market only:** Would not produce a meaningful realized PnL figure.

**Why ACB:**
- Matches Polymarket's own UI calculations, making validation against the Data API tractable.
- O(1) state per (wallet, token): just `size`, `avg_price`, `realized_pnl` — three int64s.
- Simple, correct, explainable.

**Limitation:** ACB is not tax-optimal; this is documented in `LIMITATIONS.md`.

---

## ADR-005: Fixed-Point Arithmetic — int64, Scale 1e-6

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** All quantities (shares), prices, and PnL values are stored as `int64` with implicit scale factor 1,000,000 (i.e., 1 share = 1_000_000 units, price $0.65 = 650_000 units).

**Alternatives rejected:**
- **`double` / `float64`:** IEEE 754 floating-point introduces rounding errors that accumulate over millions of trades. Unacceptable in an accounting system.
- **`__int128`:** Used for intermediate products only (see overflow analysis below), not for storage. Storing __int128 doubles memory per field and there's no SIMD lane for it.
- **Decimal128 / `boost::multiprecision`:** Overkill; adds a dependency; slower than native integers.

**Overflow analysis:**
- Max position size: `INT64_MAX / 1e6 = 9,223,372,036,854` shares ≈ 9.2 trillion shares. No real position is this large.
- Intermediate product `size × price`: `9.2e12 × 1e6 = 9.2e18`, which overflows int64 (max ~9.2e18). We use `__int128` for the numerator of the avg_price update. The result, divided by the new total size (also scaled), fits in int64.
- Documented in `ACCOUNTING_MODEL.md`.

**Why 1e-6:**
- USDC on Polygon has 6 decimal places. Polymarket prices are expressed in USDC. Scale 1e6 gives exact representation of on-chain amounts.

---

## ADR-006: Storage — ClickHouse ReplacingMergeTree

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** ClickHouse with `ReplacingMergeTree` for normalized events, keyed on `(tx_hash, log_index)`.

**Alternatives rejected:**
- **PostgreSQL:** Row-oriented; slower for analytical queries (PnL curves, top traders); no native columnar compression. Would require TimescaleDB extension for time-series which adds complexity.
- **TimescaleDB:** PostgreSQL-based; better than vanilla PG for time-series but still row-oriented at the core.
- **DuckDB:** Excellent for local analytics but not designed for concurrent write + read serving. No replication story.
- **Redis / in-memory only:** No durability, no historical query capability.

**Why ClickHouse + ReplacingMergeTree:**
- Columnar storage compresses repetitive trading data extremely well (10x+ vs row stores).
- `ReplacingMergeTree` deduplicates on `(tx_hash, log_index)` — replaying a block range is safe.
- Partition by month: efficient range scans for PnL curves.
- ORDER BY `(wallet, block_number, log_index)`: enables fast per-wallet scans.
- Known limitation: `FINAL` keyword required to force deduplication on reads; documented.

---

## ADR-007: Reorg Window — 128 Blocks

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** Maintain an unconfirmed window of 128 blocks. Events in this window can be rolled back; events older than 128 blocks are considered final.

**Rationale:** Polygon PoS uses BABE-style consensus; reorgs are rare and shallow (typically 1-5 blocks). 128 blocks (~5 minutes at ~2.5 sec/block) gives a 25,600× safety margin over typical reorg depth while keeping the rollback journal bounded in memory.

**Alternatives:** 32 blocks (too small for adversarial conditions), 256+ blocks (unnecessarily large journal).

---

## ADR-008: Leaderboard Structure — Per-Shard Incremental Heap

**Date:** 2026-11-11  
**Status:** Accepted

**Decision:** Each engine shard maintains an incremental max-heap for its wallets. The global leaderboard is computed by merge-on-read from all shard heaps (top-K merge).

**Alternatives rejected:**
- **Global sorted structure:** Requires cross-shard locking or coordination on every update.
- **Full sort on read:** O(N log N) per leaderboard request; unacceptable at millions of wallets.
- **Order-statistic tree:** O(log N) per update but higher constant than a heap; more complex.

**Why heap:**
- O(log K) per update where K = leaderboard size (default 1000).
- Leaderboard read is O(shards × K log K) which is fast.
- Fits naturally with the shard-per-thread model.

---
