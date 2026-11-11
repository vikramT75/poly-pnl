import asyncio
import json
import random
from typing import AsyncGenerator, List, Optional
import websockets
from .emitter import RawLogItem
from .hypersync_source import ALL_CONTRACTS
from .metrics import WS_RECONNECTS_TOTAL, ERROR_RATE

class LiveSubscriber:
    def __init__(self, ws_url: str, fallback_http: Optional[str] = None) -> None:
        self.ws_url = ws_url
        self.fallback_http = fallback_http
        self.running = False

    async def subscribe_logs(self) -> AsyncGenerator[RawLogItem, None]:
        self.running = True
        backoff = 1.0

        while self.running:
            try:
                async with websockets.connect(self.ws_url, ping_interval=20, ping_timeout=20) as ws:
                    WS_RECONNECTS_TOTAL.inc()
                    backoff = 1.0

                    # eth_subscribe for logs from the verified contracts
                    sub_req = {
                        "jsonrpc": "2.0",
                        "id": 1,
                        "method": "eth_subscribe",
                        "params": [
                            "logs",
                            {
                                "address": ALL_CONTRACTS
                            }
                        ]
                    }
                    await ws.send(json.dumps(sub_req))
                    res = await ws.recv()

                    while self.running:
                        msg = await ws.recv()
                        data = json.loads(msg)
                        params = data.get("params", {})
                        result = params.get("result", {})
                        if not result:
                            continue

                        addr = bytes.fromhex(result.get("address", "")[2:].lower().zfill(40))
                        topics = [bytes.fromhex(t[2:].zfill(64)) for t in result.get("topics", [])]
                        raw_data = bytes.fromhex(result.get("data", "")[2:]) if result.get("data") else b""

                        b_num = int(result.get("blockNumber", "0x0"), 16)
                        l_idx = int(result.get("logIndex", "0x0"), 16)
                        t_idx = int(result.get("transactionIndex", "0x0"), 16)
                        b_hash = result.get("blockHash", "")

                        yield RawLogItem(
                            block_number=b_num,
                            timestamp_s=int(asyncio.get_event_loop().time()),
                            log_index=l_idx,
                            tx_index=t_idx,
                            address=addr,
                            topics=topics,
                            data=raw_data,
                            block_hash=b_hash
                        )
            except Exception as e:
                ERROR_RATE.labels(type="ws_live").inc()
                jitter = random.uniform(0.5, 1.5)
                await asyncio.sleep(min(backoff * jitter, 30.0))
                backoff = min(backoff * 2.0, 30.0)

    def stop(self) -> None:
        self.running = False
