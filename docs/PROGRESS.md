# Project Progress Log

Tracks implementation status across all phases according to the master plan.

---

## Phase 0: Research & Setup
- [x] Verified Polymarket contract addresses on Polygon mainnet (CTF Exchange V1/V2, NegRisk V1/V2, CTF Gnosis, NegRisk Adapter).
- [x] Verified CLOB V2 exchange upgrade event signatures (V1 8 params vs V2 10 params).
- [x] Initialized architecture docs: `CONTRACTS_AND_APIS.md`, `ACCOUNTING_MODEL.md`, `DECISIONS.md`, `EDGE_CASES.md`, `LIMITATIONS.md`.

## Phase 1: High-Performance C++20 Decoder
- [x] Fixed-size cache-aligned `NormalizedEvent` struct (64 bytes).
- [x] Hand-written zero-copy ABI decoding for V1/V2 `OrderFilled`, `PositionSplit`, `PositionsMerge`, `PayoutRedemption`, `TransferSingle`, and `TransferBatch`.
- [x] GoogleTest unit test suite with boundary and malformed data testing (`tests/unit/decoder_test.cpp`).
- [x] libFuzzer fuzz harness (`tests/fuzz/decoder_fuzz.cpp`).
- [x] **Measured Throughput:** **3,396,849 events/sec** (294 ns/event).

## Phase 2: Ingestion Pipeline (Python 3.12+ Asyncio)
- [x] Modular package layout (`pyproject.toml`, `gravia_ingest`).
- [x] Envio HyperSync client for Polygon backfill.
- [x] Polygon RPC WebSocket `eth_subscribe` logs live listener with fallback.
- [x] 128-block reorg detection ring buffer and rollback emitter.
- [x] Atomic JSON checkpoint persistence with recovery.
- [x] Gamma API metadata synchronization worker.
- [x] CLOB WebSocket mark prices consumer.
- [x] ClickHouse ReplacingMergeTree batch writer.
- [x] Unit tests for checkpointing, reorgs, and binary emitter (`tests/test_*.py`).

## Phase 3: PnL Engine Core
- [x] Average cost basis accounting implemented with `int64_t` scale 1e-6.
- [x] Intermediate `__int128` precision for product overflow prevention.
- [x] Handling of fills, splits, merges, redeems, fee subtraction, oversell clamping, transfers.
- [x] Unit test suite (`tests/unit/engine_test.cpp`, `tests/unit/fixedpoint_test.cpp`).
- [x] **Measured Throughput:** **6,798,886 updates/sec** (147 ns/update).

## Phase 4: Live Mode, Reorg Rollbacks & Marks
- [x] 128-block RollbackJournal supporting delta reversion.
- [x] Inverted `TokenHolderIndex` for sub-millisecond unrealized PnL mark distribution.
- [x] Determinism and property testing (`tests/property/determinism_test.cpp`, `tests/property/rounding_test.cpp`).
- [x] Byte-identical state restoration on rollback + canonical replay verified.

## Phase 5: Scale-out, Serving & Frontend
- [x] Flat open-addressing `PositionMap` achieving **43.8M lookups/sec** and **14.2M inserts/sec**.
- [x] Lock-free SPSC ring buffer achieving **202M msg/sec**.
- [x] Query server and binary atomic snapshot manager with recovery.
- [x] FastAPI REST & WebSocket serving layer (`api/main.py`).
- [x] Next.js frontend with live leaderboard, wallet portfolios, and system health status.

## Phase 6: Optimization, Benchmarks & Ops
- [x] Docker Compose stack (ClickHouse, engine, ingestor, api, web, prometheus, grafana).
- [x] Production Grafana dashboard (`infra/grafana/dashboards/ingestion.json`).
- [x] GitHub Actions CI workflow with GCC/Clang matrix, sanitizers, and benchmark regression checks.
- [x] Final comprehensive documentation: `ARCHITECTURE.md`, `PERFORMANCE.md`, `VALIDATION.md`, `README.md`.
