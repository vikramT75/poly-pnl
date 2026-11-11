import asyncio
import time
from typing import Optional

from .config import IngestConfig
from .checkpoint import CheckpointManager, Checkpoint
from .reorg import ReorgTracker, BlockHeader
from .emitter import EngineEmitter, RawLogItem
from .clickhouse_writer import ClickHouseWriter
from .hypersync_source import HyperSyncSource
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
        self.live_sub = LiveSubscriber(ws_url=config.polygon_rpc_ws)
        self.metadata = GammaMetadataWorker(gamma_api_url=config.gamma_api_url)
        self.clob_ws = ClobWebSocketConsumer(ws_url=config.clob_ws_url)
        self.running = False

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

        # Collect active tokens for CLOB WS marks
        tokens = []
        for m in markets:
            if m.yes_token_id: tokens.append(m.yes_token_id)
            if m.no_token_id:  tokens.append(m.no_token_id)
        self.clob_ws.add_tokens(tokens)

        # 4. If checkpoint is 0 or needs backfill, run bounded backfill
        if last_block == 0:
            # Default ~90 days or ~10,000 blocks for quick startup demo
            # Polygon produces ~43k blocks/day. 90 days = ~3.8M blocks
            # For demonstration, target start at recent block window
            start_block = max(1, 55_000_000)
            target_block = start_block + 5_000
            print(f"[Pipeline] Starting backfill from block {start_block} to {target_block}...")

            total_events = 0
            t0 = time.time()
            async for batch in self.hypersync.stream_backfill(start_block, target_block, batch_size=1000):
                if not self.running:
                    break

                # Write to ClickHouse
                self.clickhouse.write_raw_logs(batch)

                # Emit to C++ engine
                await self.emitter.emit_batch(batch)

                total_events += len(batch)
                last_block = batch[-1].block_number
                LAST_BLOCK_PROCESSED.set(last_block)

            elapsed = max(0.001, time.time() - t0)
            print(f"[Pipeline] Backfill complete: {total_events} events processed in {elapsed:.2f}s ({total_events/elapsed:.1f} ev/s)")

            # Save checkpoint
            self.checkpoint_mgr.save(Checkpoint(
                last_block=last_block,
                last_log_index=0,
                block_hash="",
                timestamp=time.time()
            ))

        # 5. Start live processing tasks
        asyncio.create_task(self._run_live_marks())
        await self._run_live_logs()

    async def _run_live_logs(self) -> None:
        last_block_number = -1
        async for log_item in self.live_sub.subscribe_logs():
            if not self.running:
                break

            # Check reorg only when advancing or changing block height
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
            # Marks can be forwarded directly or batched
            pass

    async def stop(self) -> None:
        self.running = False
        self.live_sub.stop()
        self.clob_ws.stop()
        await self.emitter.close()
