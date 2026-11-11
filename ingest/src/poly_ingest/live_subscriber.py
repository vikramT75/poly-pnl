import asyncio
import json
import random
import time
from typing import AsyncGenerator, List, Optional
import aiohttp
import websockets
from .emitter import RawLogItem
from .hypersync_source import ALL_CONTRACTS
from .metrics import WS_RECONNECTS_TOTAL, ERROR_RATE

class LiveSubscriber:
    def __init__(self, ws_url: str, http_url: Optional[str] = None, fallback_http: Optional[str] = None) -> None:
        self.ws_url = ws_url
        self.http_url = http_url or "https://polygon-bor-rpc.publicnode.com"
        self.fallback_http = fallback_http or "https://1rpc.io/matic"
        self.running = False

    async def subscribe_logs(self, start_block: Optional[int] = None) -> AsyncGenerator[RawLogItem, None]:
        self.running = True

        # Attempt WebSocket connection if URL is provided
        if self.ws_url and "alchemy.com/v2/demo" not in self.ws_url:
            print(f"[LiveSubscriber] Attempting WebSocket connection to {self.ws_url}...")
            try:
                async with websockets.connect(self.ws_url, ping_interval=20, ping_timeout=10, open_timeout=5) as ws:
                    WS_RECONNECTS_TOTAL.inc()
                    sub_req = {
                        "jsonrpc": "2.0",
                        "id": 1,
                        "method": "eth_subscribe",
                        "params": [
                            "logs",
                            {"address": ALL_CONTRACTS}
                        ]
                    }
                    await ws.send(json.dumps(sub_req))
                    res = await ws.recv()
                    res_data = json.loads(res)
                    if "error" in res_data:
                        raise RuntimeError(f"eth_subscribe rejected: {res_data['error']}")

                    print("[LiveSubscriber] WebSocket connected and subscribed to live Polymarket events.")
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
                            timestamp_s=int(time.time()),
                            log_index=l_idx,
                            tx_index=t_idx,
                            address=addr,
                            topics=topics,
                            data=raw_data,
                            block_hash=b_hash
                        )
                    return
            except Exception as e:
                print(f"[LiveSubscriber] WebSocket subscription unavailable ({e}). Falling back to live HTTP log polling...")

        # Fallback: Live HTTP getLogs polling
        async for log_item in self.poll_logs_http(start_block=start_block):
            yield log_item

    async def poll_logs_http(self, start_block: Optional[int] = None) -> AsyncGenerator[RawLogItem, None]:
        current_block = start_block
        async with aiohttp.ClientSession() as session:
            # If start_block not given, query current head
            if current_block is None or current_block <= 0:
                for rpc in [self.http_url, self.fallback_http]:
                    try:
                        payload = {"jsonrpc": "2.0", "method": "eth_blockNumber", "params": [], "id": 1}
                        async with session.post(rpc, json=payload, timeout=aiohttp.ClientTimeout(total=5)) as resp:
                            if resp.status == 200:
                                data = await resp.json()
                                current_block = int(data.get("result", "0x0"), 16)
                                break
                    except Exception:
                        continue
                if current_block is None:
                    current_block = 97_133_000

            print(f"[LiveSubscriber] Polling Polygon RPC ({self.http_url}) starting at block {current_block}...")

            while self.running:
                try:
                    head_payload = {"jsonrpc": "2.0", "method": "eth_blockNumber", "params": [], "id": 1}
                    async with session.post(self.http_url, json=head_payload, timeout=aiohttp.ClientTimeout(total=5)) as resp:
                        if resp.status != 200:
                            await asyncio.sleep(2.0)
                            continue
                        data = await resp.json()
                        head_block = int(data.get("result", "0x0"), 16)

                    if head_block >= current_block:
                        to_b = min(current_block + 20, head_block)
                        logs_payload = {
                            "jsonrpc": "2.0",
                            "method": "eth_getLogs",
                            "params": [{
                                "fromBlock": hex(current_block),
                                "toBlock": hex(to_b),
                                "address": ALL_CONTRACTS
                            }],
                            "id": 2
                        }
                        async with session.post(self.http_url, json=logs_payload, timeout=aiohttp.ClientTimeout(total=10)) as resp:
                            if resp.status == 200:
                                res_json = await resp.json()
                                logs = res_json.get("result", [])
                                now = int(time.time())
                                for log in logs:
                                    addr = bytes.fromhex(log.get("address", "")[2:].lower().zfill(40))
                                    topics = [bytes.fromhex(t[2:].zfill(64)) for t in log.get("topics", [])]
                                    raw_data = bytes.fromhex(log.get("data", "")[2:]) if log.get("data") else b""
                                    b_num = int(log.get("blockNumber", "0x0"), 16)
                                    l_idx = int(log.get("logIndex", "0x0"), 16)
                                    t_idx = int(log.get("transactionIndex", "0x0"), 16)
                                    b_hash = log.get("blockHash", "")

                                    yield RawLogItem(
                                        block_number=b_num,
                                        timestamp_s=now,
                                        log_index=l_idx,
                                        tx_index=t_idx,
                                        address=addr,
                                        topics=topics,
                                        data=raw_data,
                                        block_hash=b_hash
                                    )
                                current_block = to_b + 1
                    await asyncio.sleep(2.0)
                except Exception as e:
                    ERROR_RATE.labels(type="http_poll").inc()
                    await asyncio.sleep(2.0)

    def stop(self) -> None:
        self.running = False
