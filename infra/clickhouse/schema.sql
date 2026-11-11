-- ClickHouse Schema for gravia-pnl
-- All tables use ReplacingMergeTree for automatic idempotency during replay

CREATE DATABASE IF NOT EXISTS gravia;

-- 1. Raw Logs: Ingested on-chain logs from Polygon
CREATE TABLE IF NOT EXISTS gravia.raw_logs
(
    block_number    UInt64,
    tx_hash         FixedString(66),
    log_index       UInt32,
    tx_index        UInt32,
    timestamp       DateTime,
    contract_addr   FixedString(42),
    topic0          FixedString(66),
    topic1          FixedString(66),
    topic2          FixedString(66),
    topic3          FixedString(66),
    num_topics      UInt8,
    data            String,
    _version        UInt64 DEFAULT toUnixTimestamp64Milli(now64())
)
ENGINE = ReplacingMergeTree(_version)
PARTITION BY toYYYYMM(timestamp)
ORDER BY (block_number, log_index)
SETTINGS index_granularity = 8192;

-- 2. Normalized Events: Output of C++ Decoder
CREATE TABLE IF NOT EXISTS gravia.events
(
    block_number    UInt64,
    log_index       UInt32,
    tx_index        UInt32,
    timestamp       DateTime,
    wallet          FixedString(42),
    token_id        String,
    qty             Int64,             -- 1e-6 scale
    price           Int64,             -- 1e-6 USDC
    fee             Int32,
    kind            UInt8,
    side            UInt8,
    flags           UInt8,
    aux_token_id    String,
    payout_price    Int32,
    _version        UInt64 DEFAULT toUnixTimestamp64Milli(now64())
)
ENGINE = ReplacingMergeTree(_version)
PARTITION BY toYYYYMM(timestamp)
ORDER BY (wallet, block_number, log_index)
SETTINGS index_granularity = 8192;

-- 3. Block Hashes: Used for reorg detection and fork points
CREATE TABLE IF NOT EXISTS gravia.block_hashes
(
    block_number    UInt64,
    block_hash      FixedString(66),
    parent_hash     FixedString(66),
    timestamp       DateTime,
    _version        UInt64 DEFAULT toUnixTimestamp64Milli(now64())
)
ENGINE = ReplacingMergeTree(_version)
ORDER BY block_number;

-- 4. Market Metadata from Gamma API
CREATE TABLE IF NOT EXISTS gravia.markets
(
    condition_id    FixedString(66),
    question        String,
    yes_token_id    String,
    no_token_id     String,
    resolved        UInt8,
    payout_yes      Int32,
    payout_no       Int32,
    updated_at      DateTime,
    _version        UInt64 DEFAULT toUnixTimestamp64Milli(now64())
)
ENGINE = ReplacingMergeTree(_version)
ORDER BY condition_id;

-- 5. PnL Snapshots: Daily/Hourly aggregates for PnL curves
CREATE TABLE IF NOT EXISTS gravia.pnl_snapshots
(
    wallet          FixedString(42),
    snapshot_time   DateTime,
    block_number    UInt64,
    realized_pnl    Int64,
    unrealized_pnl  Int64,
    total_pnl       Int64,
    volume          Int64
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(snapshot_time)
ORDER BY (wallet, snapshot_time)
TTL snapshot_time + INTERVAL 1 YEAR;
