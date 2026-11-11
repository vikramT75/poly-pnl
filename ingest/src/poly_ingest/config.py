from pydantic import Field
from pydantic_settings import BaseSettings


class IngestConfig(BaseSettings):
    # Polygon RPC
    polygon_rpc_ws: str = Field(
        default="wss://polygon-bor-rpc.publicnode.com", validation_alias="POLYGON_RPC_WS"
    )
    polygon_rpc_http: str = Field(
        default="https://polygon-bor-rpc.publicnode.com", validation_alias="POLYGON_RPC_HTTP"
    )
    polygon_rpc_fallback_http: str = Field(
        default="https://1rpc.io/matic", validation_alias="POLYGON_RPC_FALLBACK_HTTP"
    )

    # Indexer (Envio HyperSync)
    hypersync_url: str = Field(
        default="https://polygon.hypersync.xyz", validation_alias="HYPERSYNC_URL"
    )

    # Storage
    clickhouse_host: str = Field(default="localhost", validation_alias="CLICKHOUSE_HOST")
    clickhouse_port: int = Field(default=8123, validation_alias="CLICKHOUSE_PORT")
    clickhouse_user: str = Field(default="default", validation_alias="CLICKHOUSE_USER")
    clickhouse_password: str = Field(default="", validation_alias="CLICKHOUSE_PASSWORD")
    clickhouse_database: str = Field(default="polypnl", validation_alias="CLICKHOUSE_DATABASE")

    # Ingestion parameters
    backfill_days: int = Field(default=90, validation_alias="BACKFILL_DAYS")
    reorg_window: int = Field(default=128, validation_alias="REORG_WINDOW")  # ADR-007
    batch_size: int = Field(default=10000, validation_alias="BATCH_SIZE")

    # Engine Socket IPC
    engine_host: str = Field(default="127.0.0.1", validation_alias="ENGINE_HOST")
    engine_port: int = Field(default=9999, validation_alias="ENGINE_PORT")
    engine_socket_path: str = Field(
        default="/tmp/poly_engine.sock", validation_alias="ENGINE_SOCKET"
    )

    # Checkpoint
    checkpoint_path: str = Field(
        default="./data/checkpoint.json", validation_alias="CHECKPOINT_PATH"
    )

    # Metrics & Observability
    metrics_port: int = Field(default=9090, validation_alias="METRICS_PORT")

    # External APIs
    gamma_api_url: str = Field(
        default="https://gamma-api.polymarket.com", validation_alias="GAMMA_API_URL"
    )
    clob_ws_url: str = Field(
        default="wss://ws-subscriptions-clob.polymarket.com/ws/market",
        validation_alias="CLOB_WS_URL",
    )

    class Config:
        env_file = ".env"
        extra = "ignore"
