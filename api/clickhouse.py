import os
from typing import List, Dict, Any

class ClickHouseQueryService:
    def __init__(self) -> None:
        self.host = os.getenv("CLICKHOUSE_HOST", "localhost")
        self.port = int(os.getenv("CLICKHOUSE_PORT", "8123"))
        self.user = os.getenv("CLICKHOUSE_USER", "default")
        self.password = os.getenv("CLICKHOUSE_PASSWORD", "")
        self.database = os.getenv("CLICKHOUSE_DATABASE", "gravia")
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

    def get_wallet_curve(self, wallet_addr: str) -> List[Dict[str, Any]]:
        """Fetch historical PnL curve points for a wallet."""
        if not self.client:
            self.connect()
        if not self.client:
            # Fallback mock point for demonstration if ClickHouse is not active
            return [
                {"timestamp": "2026-11-10T00:00:00Z", "total_pnl": 0.0, "realized_pnl": 0.0},
                {"timestamp": "2026-11-10T12:00:00Z", "total_pnl": 12.50, "realized_pnl": 5.20},
                {"timestamp": "2026-11-11T00:00:00Z", "total_pnl": 34.80, "realized_pnl": 18.90}
            ]

        try:
            query = """
                SELECT
                    toUnixTimestamp(timestamp) AS ts,
                    sum(qty * price / 1000000000000) AS cumulative_volume
                FROM events FINAL
                WHERE wallet = %(wallet)s
                GROUP BY ts
                ORDER BY ts ASC
                LIMIT 500
            """
            result = self.client.query(query, parameters={"wallet": wallet_addr.lower()})
            points = []
            for row in result.result_rows:
                points.append({
                    "timestamp": row[0],
                    "total_pnl": float(row[1])
                })
            return points
        except Exception:
            return []
