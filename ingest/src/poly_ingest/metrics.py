from prometheus_client import Counter, Gauge, start_http_server

# Metrics specified in Master Prompt Section 5.1
BLOCKS_BEHIND_HEAD = Gauge(
    "poly_ingest_blocks_behind_head", "Estimated number of blocks behind chain head"
)

EVENTS_PER_SECOND = Gauge(
    "poly_ingest_events_per_second", "Current event ingestion throughput per second"
)

REORGS_TOTAL = Counter(
    "poly_ingest_reorgs_total", "Total number of blockchain reorgs detected and rolled back"
)

GAP_FILLS_TOTAL = Counter(
    "poly_ingest_gap_fills_total", "Total number of missing block range reconciliations executed"
)

WS_RECONNECTS_TOTAL = Counter(
    "poly_ingest_ws_reconnects_total", "Total WebSocket disconnections and reconnects"
)

QUEUE_DEPTH = Gauge(
    "poly_ingest_queue_depth", "Number of raw logs queued for engine socket delivery"
)

ERROR_RATE = Counter(
    "poly_ingest_error_rate", "Total errors encountered in ingestion loop", ["type"]
)

LAST_BLOCK_PROCESSED = Gauge(
    "poly_ingest_last_block_processed", "Latest block number committed to checkpoint"
)

CLICKHOUSE_ROWS_WRITTEN = Counter(
    "poly_clickhouse_rows_written_total", "Total rows written to ClickHouse", ["table"]
)

CLICKHOUSE_WRITE_ERRORS = Counter(
    "poly_clickhouse_write_errors_total", "ClickHouse batch insertion errors"
)


def start_metrics_server(port: int) -> None:
    """Start the Prometheus metrics exporter endpoint."""
    start_http_server(port)
