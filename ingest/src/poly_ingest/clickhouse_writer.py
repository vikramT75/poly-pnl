from typing import List, Optional
import datetime
from .emitter import RawLogItem
from .reorg import BlockHeader
from .metrics import CLICKHOUSE_ROWS_WRITTEN, CLICKHOUSE_WRITE_ERRORS

class ClickHouseWriter:
    def __init__(self, host: str = "localhost", port: int = 8123,
                 user: str = "default", password: str = "", database: str = "polypnl") -> None:
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        self.database = database
        self.client = None

    def connect(self) -> None:
        try:
            import clickhouse_connect
            self.client = clickhouse_connect.get_client(
                host=self.host,
                port=self.port,
                username=self.user,
                password=self.password,
                database=self.database
            )
        except Exception:
            self.client = None

    def write_raw_logs(self, items: List[RawLogItem]) -> None:
        if not items:
            return
        if not self.client:
            self.connect()
        if not self.client:
            return

        rows = []
        now = datetime.datetime.utcnow()
        for item in items:
            t0 = "0x" + item.topics[0].hex() if len(item.topics) > 0 else "0x" + "00"*32
            t1 = "0x" + item.topics[1].hex() if len(item.topics) > 1 else "0x" + "00"*32
            t2 = "0x" + item.topics[2].hex() if len(item.topics) > 2 else "0x" + "00"*32
            t3 = "0x" + item.topics[3].hex() if len(item.topics) > 3 else "0x" + "00"*32
            rows.append([
                item.block_number,
                "0x" + "00"*32, # tx_hash
                item.log_index,
                item.tx_index,
                datetime.datetime.utcfromtimestamp(item.timestamp_s) if item.timestamp_s else now,
                "0x" + item.address.hex(),
                t0,
                t1,
                t2,
                t3,
                len(item.topics),
                item.data.hex()
            ])

        try:
            self.client.insert(
                "raw_logs",
                rows,
                column_names=[
                    "block_number", "tx_hash", "log_index", "tx_index", "timestamp",
                    "contract_addr", "topic0", "topic1", "topic2", "topic3", "num_topics", "data"
                ]
            )
            CLICKHOUSE_ROWS_WRITTEN.labels(table="raw_logs").inc(len(rows))
        except Exception:
            CLICKHOUSE_WRITE_ERRORS.inc()

    def write_block_header(self, block: BlockHeader) -> None:
        if not self.client:
            self.connect()
        if not self.client:
            return
        try:
            now = datetime.datetime.utcnow()
            self.client.insert(
                "block_hashes",
                [[block.number, block.block_hash, block.parent_hash, now]],
                column_names=["block_number", "block_hash", "parent_hash", "timestamp"]
            )
            CLICKHOUSE_ROWS_WRITTEN.labels(table="block_hashes").inc(1)
        except Exception:
            CLICKHOUSE_WRITE_ERRORS.inc()
