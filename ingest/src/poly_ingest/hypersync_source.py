import asyncio
import aiohttp
from typing import AsyncGenerator, List, Dict, Any, Optional
from .emitter import RawLogItem

# Verified contract addresses from docs/CONTRACTS_AND_APIS.md
CTF_EXCHANGE_V1 = "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E".lower()
NEGRISK_EXCHANGE_V1 = "0xC5d563A36AE78145C45a50134d48A1215220f80a".lower()
CTF_EXCHANGE_V2 = "0xE111180000d2663C0091e4f400237545B87B996B".lower()
NEGRISK_EXCHANGE_V2 = "0xe2222d279d744050d28e00520010520000310F59".lower()
CTF_CONTRACT = "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045".lower()
NEGRISK_ADAPTER = "0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296".lower()

ALL_CONTRACTS = [
    CTF_EXCHANGE_V1, NEGRISK_EXCHANGE_V1,
    CTF_EXCHANGE_V2, NEGRISK_EXCHANGE_V2,
    CTF_CONTRACT, NEGRISK_ADAPTER
]

class HyperSyncSource:
    def __init__(self, hypersync_url: str = "https://polygon.hypersync.xyz") -> None:
        self.url = hypersync_url.rstrip("/")

    async def stream_backfill(
        self,
        from_block: int,
        to_block: int,
        batch_size: int = 1000
    ) -> AsyncGenerator[List[RawLogItem], None]:
        """
        Query HyperSync JSON query endpoint for raw logs across the target contracts.
        Yields batches of RawLogItem.
        """
        query_payload = {
            "from_block": from_block,
            "to_block": to_block,
            "logs": [
                {
                    "address": ALL_CONTRACTS
                }
            ],
            "field_selection": {
                "log": [
                    "block_number",
                    "log_index",
                    "transaction_index",
                    "data",
                    "address",
                    "topic0",
                    "topic1",
                    "topic2",
                    "topic3"
                ],
                "block": [
                    "number",
                    "timestamp"
                ]
            }
        }

        async with aiohttp.ClientSession() as session:
            current_from = from_block
            while current_from <= to_block:
                payload = dict(query_payload)
                payload["from_block"] = current_from
                payload["to_block"] = min(current_from + batch_size * 5, to_block)

                try:
                    async with session.post(f"{self.url}/query", json=payload, timeout=30) as resp:
                        if resp.status == 200:
                            data = await resp.json()
                            logs_raw = data.get("data", {}).get("logs", [])
                            if not logs_raw:
                                current_from = payload["to_block"] + 1
                                continue

                            items: List[RawLogItem] = []
                            for log in logs_raw:
                                addr = bytes.fromhex(log.get("address", "")[2:].lower().zfill(40))
                                topics = []
                                for t_idx in range(4):
                                    key = f"topic{t_idx}"
                                    if key in log and log[key]:
                                        topics.append(bytes.fromhex(log[key][2:].zfill(64)))
                                raw_data = bytes.fromhex(log.get("data", "")[2:]) if log.get("data") else b""

                                items.append(RawLogItem(
                                    block_number=int(log.get("block_number", 0)),
                                    timestamp_s=int(log.get("timestamp", 0)),
                                    log_index=int(log.get("log_index", 0)),
                                    tx_index=int(log.get("transaction_index", 0)),
                                    address=addr,
                                    topics=topics,
                                    data=raw_data
                                ))

                            if items:
                                yield items
                            next_block = data.get("next_block")
                            if next_block:
                                current_from = int(next_block)
                            else:
                                current_from = payload["to_block"] + 1
                        else:
                            # Back off on rate limits or errors
                            await asyncio.sleep(2.0)
                            current_from = payload["to_block"] + 1
                except Exception:
                    await asyncio.sleep(2.0)
                    current_from += batch_size
