# Architecture & System Design

`poly-pnl` is a high-performance, real-time Profit and Loss (PnL) engine designed to track 2M+ Polymarket traders on Polygon with incremental updates and sub-100 microsecond leaderboard recalculation.

---

## 1. End-to-End System Topology

```
   ┌────────────────────────────────────────────────────────┐
   │                    POLYGON BLOCKCHAIN                  │
   └───────────────┬────────────────────────┬───────────────┘
                   │ (backfill)             │ (live logs)
                   ▼                        ▼
        ┌──────────────────────┐  ┌───────────────────┐
        │   Envio HyperSync    │  │ Polygon RPC (WS)  │
        └──────────┬───────────┘  └─────────┬─────────┘
                   │                        │
                   └───────────┬────────────┘
                               ▼
            ┌──────────────────────────────────────┐
            │       INGESTION LAYER (Python)       │
            │  - asyncio pipeline with uvloop      │
            │  - 128-block reorg detection         │
            │  - Idempotent ClickHouse bulk writer │
            │  - Gamma API metadata cache          │
            │  - CLOB WS mark price consumer       │
            └──────────────────┬───────────────────┘
                               │
                               │ Length-Prefixed Binary Frames
                               │ (1208-byte RawLog / NormalizedEvent)
                               ▼
            ┌──────────────────────────────────────┐
            │        HOT PATH ENGINE (C++20)       │
            │  - Hand-written zero-copy ABI decoder│
            │  - Fixed-point 1e-6 integer math     │
            │  - Flat open-addressing PositionMap  │
            │  - TokenHolderIndex inverted index   │
            │  - 128-block rollback journal        │
            │  - Top-K heap leaderboard            │
            │  - Atomic binary snapshotting        │
            └──────────┬────────────────┬──────────┘
                       │                │
            Binary Snapshots            │ Binary Query Protocol
            (mmap-friendly)             │ (port 9998)
                       │                ▼
                       │    ┌───────────────────────────┐
                       │    │    API SERVER (FastAPI)   │
                       └───►│  - REST endpoints         │
                            │  - Real-time WebSocket    │
                            │  - ClickHouse analytics   │
                            └─────────────┬─────────────┘
                                          │
                                          ▼
                            ┌───────────────────────────┐
                            │    NEXT.JS WEB FRONTEND   │
                            │  - Real-time terminal UI  │
                            │  - Wallet portfolio view  │
                            │  - Live leaderboard       │
                            │  - System health widget   │
                            └───────────────────────────┘
```

---

## 2. Ingestion to Engine IPC Protocol

Communication between the Python ingestor and C++ engine uses Unix Domain Sockets (or local loopback TCP in development/containers) with length-prefixed binary frames:

```
[4 bytes: frame_length (uint32_t LE)] [Payload]
```

### Frame Formats:
1. **RawLog Frame (`frame_length == 1208`):**
   Fixed-size packed `RawLog` struct containing Polygon block number, timestamp, log index, tx index, contract address, 4 topic hashes, and ABI data payload.
2. **NormalizedEvent Batch (`frame_length % 64 == 0`):**
   Batches of 64-byte `NormalizedEvent` structs directly ingested.
3. **Rollback Sentinel (`frame_length == 12`):**
   `"ROLL"` (4 bytes magic) followed by `uint64_t rollback_block`. Instructs the C++ engine to revert positions to pre-reorg state.

---

## 3. Position and PnL State Model

Each `(wallet, token)` position is stored in `PositionMap`, a flat open-addressing hash table with linear probing:
- 48 bytes per position struct
- `size`: shares held (1e-6 units)
- `avg_price`: average cost basis (1e-6 USDC)
- `realized_pnl`: cumulative realized PnL minus fees
- `running_unrealized`: cached `size * (mark - avg_price)`

### Mark Price Distribution:
Instead of iterating across all wallets when a market price updates, the engine maintains an inverted `TokenHolderIndex` mapping `token_idx -> list of wallet_idx`.
When mark changes by `delta`:
$$\Delta \text{Unrealized} = \text{size} \times \frac{\Delta \text{mark}}{10^6}$$
This is applied directly to token holders with updates coalesced into ~50-100 ms batches.

---

## 4. Reorg & Self-Healing Pipeline

1. **Polygon Reorgs:** Polygon's consensus can occasionally produce shallow reorgs (1-5 blocks).
2. **Ingestor ReorgTracker:** Maintains a 128-block ring buffer of `(block_number, block_hash, parent_hash)`.
3. **Fork Point Resolution:** When block $N$'s `parent_hash` does not match block $N-1$'s canonical hash, the tracker walks back to find the fork block $F$, pops canonical blocks $[F+1 \dots N-1]$, and emits a `ROLLBACK F` event.
4. **Engine Rollback Journal:** The C++ engine maintains inverse position deltas for the last 128 blocks. On rollback, it restores the pre-event snapshots in reverse order, leaving state byte-identical to the canonical chain.
