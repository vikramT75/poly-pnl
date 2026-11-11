import asyncio
import aiohttp
import json
from dataclasses import dataclass
from typing import Dict, List, Optional

@dataclass
class MarketMetadata:
    condition_id: str
    question: str
    yes_token_id: str
    no_token_id: str
    resolved: bool
    payout_yes: int  # 0 or 1_000_000
    payout_no: int

class GammaMetadataWorker:
    def __init__(self, gamma_api_url: str = "https://gamma-api.polymarket.com") -> None:
        self.api_url = gamma_api_url.rstrip("/")
        self.cache: Dict[str, MarketMetadata] = {} # condition_id -> MarketMetadata
        self.token_to_condition: Dict[str, str] = {} # token_id -> condition_id

    async def fetch_markets(self, limit: int = 100) -> List[MarketMetadata]:
        url = f"{self.api_url}/markets?limit={limit}&active=true"
        markets: List[MarketMetadata] = []

        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(url, timeout=10) as resp:
                    if resp.status == 200:
                        data = await resp.json()
                        for item in data:
                            cond_id = item.get("conditionId", "")
                            if not cond_id:
                                continue

                            # Parse tokens / clobTokenIds
                            clob_token_ids = item.get("clobTokenIds", [])
                            if isinstance(clob_token_ids, str):
                                try:
                                    clob_token_ids = json.loads(clob_token_ids)
                                except Exception:
                                    clob_token_ids = []

                            yes_id = clob_token_ids[0] if len(clob_token_ids) > 0 else ""
                            no_id  = clob_token_ids[1] if len(clob_token_ids) > 1 else ""

                            resolved = bool(item.get("resolved", False))
                            meta = MarketMetadata(
                                condition_id=cond_id.lower(),
                                question=item.get("question", ""),
                                yes_token_id=yes_id,
                                no_token_id=no_id,
                                resolved=resolved,
                                payout_yes=1_000_000 if resolved else 0,
                                payout_no=0
                            )
                            markets.append(meta)
                            self.cache[meta.condition_id] = meta
                            if yes_id:
                                self.token_to_condition[yes_id] = meta.condition_id
                            if no_id:
                                self.token_to_condition[no_id] = meta.condition_id
        except Exception:
            pass

        return markets

    async def sync_loop(self, interval_seconds: int = 60) -> None:
        while True:
            await self.fetch_markets()
            await asyncio.sleep(interval_seconds)

    def get_market(self, condition_id: str) -> Optional[MarketMetadata]:
        return self.cache.get(condition_id.lower())
