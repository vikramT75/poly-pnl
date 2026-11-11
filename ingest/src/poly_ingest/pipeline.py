import asyncio
import time
from typing import AsyncGenerator, List, Optional
import aiohttp

from .config import IngestConfig
from .checkpoint import CheckpointManager, Checkpoint
from .reorg import ReorgTracker, BlockHeader
from .emitter import EngineEmitter, RawLogItem
from .clickhouse_writer import ClickHouseWriter
from .hypersync_source import HyperSyncSource, ALL_CONTRACTS
from .live_subscriber import LiveSubscriber
from .metadata import GammaMetadataWorker
from .clob_ws import ClobWebSocketConsumer
from .metrics import (
    BLOCKS_BEHIND_HEAD,
    EVENTS_PER_SECOND,
    REORGS_TOTAL,
    LAST_BLOCK_PROCESSED,
    QUEUE_DEPTH
)

class IngestionPipeline:
    def __init__(self, config: IngestConfig) -> None:
        self.config = config
        self.checkpoint_mgr = CheckpointManager(config.checkpoint_path)
        self.reorg_tracker = ReorgTracker(window_size=config.reorg_window)
        self.emitter = EngineEmitter(host=config.engine_host, port=config.engine_port)
        self.clickhouse = ClickHouseWriter(
            host=config.clickhouse_host,
            port=config.clickhouse_port,
            user=config.clickhouse_user,
            password=config.clickhouse_password,
            database=config.clickhouse_database
        )
        self.hypersync = HyperSyncSource(hypersync_url=config.hypersync_url)
        self.live_sub = LiveSubscriber(
            ws_url=config.polygon_rpc_ws,
            http_url=config.polygon_rpc_http,
            fallback_http=config.polygon_rpc_fallback_http
        )
        self.metadata = GammaMetadataWorker(gamma_api_url=config.gamma_api_url)
        self.clob_ws = ClobWebSocketConsumer(ws_url=config.clob_ws_url)
        self.running = False

    async def fetch_current_head(self) -> int:
        for rpc in [self.config.polygon_rpc_http, self.config.polygon_rpc_fallback_http, "https://polygon-bor-rpc.publicnode.com", "https://1rpc.io/matic"]:
            try:
                async with aiohttp.ClientSession() as session:
                    payload = {"jsonrpc": "2.0", "method": "eth_blockNumber", "params": [], "id": 1}
                    async with session.post(rpc, json=payload, timeout=aiohttp.ClientTimeout(total=5)) as resp:
                        if resp.status == 200:
                            data = await resp.json()
                            return int(data.get("result", "0x0"), 16)
            except Exception:
                continue
        return 97_133_100

    async def stream_rpc_logs(
        self,
        from_block: int,
        to_block: int,
        batch_size: int = 25
    ) -> AsyncGenerator[List[RawLogItem], None]:
        rpc_url = self.config.polygon_rpc_http or "https://polygon-bor-rpc.publicnode.com"
        curr = from_block
        async with aiohttp.ClientSession() as session:
            while curr <= to_block and self.running:
                chunk_to = min(curr + batch_size - 1, to_block)
                payload = {
                    "jsonrpc": "2.0",
                    "method": "eth_getLogs",
                    "params": [{
                        "fromBlock": hex(curr),
                        "toBlock": hex(chunk_to),
                        "address": ALL_CONTRACTS
                    }],
                    "id": 1
                }
                try:
                    async with session.post(rpc_url, json=payload, timeout=aiohttp.ClientTimeout(total=15)) as resp:
                        if resp.status == 200:
                            data = await resp.json()
                            raw_logs = data.get("result", [])
                            items: List[RawLogItem] = []
                            now = int(time.time())
                            for log in raw_logs:
                                addr = bytes.fromhex(log.get("address", "")[2:].lower().zfill(40))
                                topics = [bytes.fromhex(t[2:].zfill(64)) for t in log.get("topics", [])]
                                raw_data = bytes.fromhex(log.get("data", "")[2:]) if log.get("data") else b""
                                b_num = int(log.get("blockNumber", "0x0"), 16)
                                l_idx = int(log.get("logIndex", "0x0"), 16)
                                t_idx = int(log.get("transactionIndex", "0x0"), 16)
                                b_hash = log.get("blockHash", "")

                                items.append(RawLogItem(
                                    block_number=b_num,
                                    timestamp_s=now,
                                    log_index=l_idx,
                                    tx_index=t_idx,
                                    address=addr,
                                    topics=topics,
                                    data=raw_data,
                                    block_hash=b_hash
                                ))
                            if items:
                                yield items
                except Exception as e:
                    print(f"[Pipeline] Warning: RPC error querying blocks {curr}-{chunk_to}: {e}")
                curr = chunk_to + 1

    async def start(self) -> None:
        self.running = True

        # 1. Load checkpoint
        cp = self.checkpoint_mgr.load()
        last_block = cp.last_block
        LAST_BLOCK_PROCESSED.set(last_block)

        # 2. Connect to engine socket and ClickHouse
        await self.emitter.connect()
        self.clickhouse.connect()

        # 3. Start background Gamma API metadata sync
        asyncio.create_task(self.metadata.sync_loop(interval_seconds=60))
        markets = await self.metadata.fetch_markets()

        tokens = []
        for m in markets:
            if m.yes_token_id: tokens.append(m.yes_token_id)
            if m.no_token_id:  tokens.append(m.no_token_id)
        self.clob_ws.add_tokens(tokens)

        # 4. If checkpoint is 0 or obsolete, sync to recent head
        current_head = await self.fetch_current_head()
        if last_block == 0 or (current_head - last_block > 5000):
            start_block = max(1, current_head - 100)
            target_block = current_head
            print(f"[Pipeline] Syncing to live chain tip: loading blocks {start_block} to {target_block} (head={current_head})...")

            total_events = 0
            t0 = time.time()
            async for batch in self.stream_rpc_logs(start_block, target_block, batch_size=25):
                if not self.running:
                    break

                self.clickhouse.write_raw_logs(batch)
                await self.emitter.emit_batch(batch)

                total_events += len(batch)
                last_block = batch[-1].block_number
                LAST_BLOCK_PROCESSED.set(last_block)

            elapsed = max(0.001, time.time() - t0)
            print(f"[Pipeline] Initial history loaded: {total_events} events processed in {elapsed:.2f}s ({total_events/elapsed:.1f} ev/s)")

            last_block = target_block
            self.checkpoint_mgr.save(Checkpoint(
                last_block=last_block,
                last_log_index=0,
                block_hash="",
                timestamp=time.time()
            ))

        # 5. Start live processing tasks
        asyncio.create_task(self._run_live_marks())
        await self._run_live_logs(start_block=last_block + 1)

    async def _run_live_logs(self, start_block: Optional[int] = None) -> None:
        last_block_number = -1
        async for log_item in self.live_sub.subscribe_logs(start_block=start_block):
            if not self.running:
                break

            # Check reorg only when block changes
            if log_item.block_number != last_block_number:
                block_hdr = BlockHeader(
                    number=log_item.block_number,
                    block_hash=log_item.block_hash,
                    parent_hash=""
                )
                rolled_back = self.reorg_tracker.add_block(block_hdr)
                if rolled_back:
                    min_rollback = min(rolled_back)
                    REORGS_TOTAL.inc()
                    print(f"[Pipeline] Reorg detected! Rolling back engine to block {min_rollback}")
                    await self.emitter.emit_rollback(min_rollback)
                last_block_number = log_item.block_number

            # Persist and emit
            self.clickhouse.write_raw_logs([log_item])
            await self.emitter.emit_log(log_item)

            LAST_BLOCK_PROCESSED.set(log_item.block_number)

    async def _run_live_marks(self) -> None:
        async for mark in self.clob_ws.stream_marks():
            if not self.running:
                break
            pass

    async def stop(self) -> None:
        self.running = False
        self.live_sub.stop()
        self.clob_ws.stop()
        await self.emitter.close()
