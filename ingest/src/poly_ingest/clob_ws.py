import asyncio
import json
from collections.abc import AsyncGenerator
from dataclasses import dataclass

import websockets


@dataclass
class MarkUpdateItem:
    token_id: str
    mark_price: int  # 1e-6 scale (e.g. $0.65 -> 650_000)
    timestamp_ms: int


class ClobWebSocketConsumer:
    def __init__(
        self, ws_url: str = "wss://ws-subscriptions-clob.polymarket.com/ws/market"
    ) -> None:
        self.ws_url = ws_url
        self.subscribed_tokens: set[str] = set()
        self.running = False

    def add_tokens(self, tokens: list[str]) -> None:
        for t in tokens:
            if t:
                self.subscribed_tokens.add(t)

    async def stream_marks(self) -> AsyncGenerator[MarkUpdateItem, None]:
        self.running = True

        while self.running:
            try:
                async with websockets.connect(self.ws_url, ping_interval=None) as ws:
                    # Subscribe if tokens available
                    if self.subscribed_tokens:
                        sub_msg = {
                            "assets_ids": list(self.subscribed_tokens)[:100],
                            "type": "market",
                            "initial_dump": True,
                            "custom_feature_enabled": True,
                        }
                        await ws.send(json.dumps(sub_msg))

                    # Background keepalive task: send "PING" every 10 seconds
                    async def keepalive() -> None:
                        while self.running:
                            await asyncio.sleep(10.0)
                            try:
                                await ws.send("PING")
                            except Exception:
                                break

                    ping_task = asyncio.create_task(keepalive())

                    try:
                        while self.running:
                            msg = await ws.recv()
                            if msg == "PONG":
                                continue

                            try:
                                data = json.loads(msg)
                            except Exception:
                                continue

                            event_type = data.get("event_type")
                            if event_type in ("price_change", "last_trade_price"):
                                asset_id = str(data.get("asset_id", ""))
                                price_val = float(data.get("price", 0.0))
                                if price_val > 0:
                                    int_price = int(price_val * 1_000_000)
                                    yield MarkUpdateItem(
                                        token_id=asset_id,
                                        mark_price=int_price,
                                        timestamp_ms=int(asyncio.get_event_loop().time() * 1000),
                                    )
                    finally:
                        ping_task.cancel()
            except Exception:
                await asyncio.sleep(2.0)

    def stop(self) -> None:
        self.running = False
