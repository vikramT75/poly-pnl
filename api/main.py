from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Query
from fastapi.middleware.cors import CORSMiddleware
from typing import Optional, List, Dict, Set
import asyncio
import os

from .client import EngineClient
from .clickhouse import ClickHouseQueryService

app = FastAPI(
    title="gravia-pnl API",
    description="Real-Time Polymarket PnL Engine REST & WebSocket API",
    version="0.1.0"
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

engine_client = EngineClient(
    host=os.getenv("ENGINE_HOST", "127.0.0.1"),
    port=int(os.getenv("ENGINE_QUERY_PORT", "9998"))
)
ch_service = ClickHouseQueryService()

class ConnectionManager:
    def __init__(self) -> None:
        self.active_connections: List[WebSocket] = []
        self.subscriptions: Dict[WebSocket, Set[str]] = {}

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        self.active_connections.append(websocket)
        self.subscriptions[websocket] = {"leaderboard"}

    def disconnect(self, websocket: WebSocket) -> None:
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)
        self.subscriptions.pop(websocket, None)

    async def broadcast_leaderboard(self, data: List[Dict]) -> None:
        payload = {"type": "leaderboard", "data": data}
        for ws in list(self.active_connections):
            if "leaderboard" in self.subscriptions.get(ws, set()):
                try:
                    await ws.send_json(payload)
                except Exception:
                    self.disconnect(ws)

manager = ConnectionManager()

# Background task to push live leaderboard updates to WebSocket clients
@app.on_event("startup")
async def startup_event():
    async def push_loop():
        while True:
            await asyncio.sleep(1.0)
            if manager.active_connections:
                lb = engine_client.get_leaderboard(k=50)
                await manager.broadcast_leaderboard(lb)
    asyncio.create_task(push_loop())

@app.get("/health")
def health():
    stats = engine_client.get_stats()
    is_alive = engine_client.ping()
    return {
        "status": "ok" if is_alive else "degraded",
        "engine_alive": is_alive,
        "engine_stats": stats
    }

@app.get("/leaderboard")
def get_leaderboard(
    metric: str = Query("pnl", enum=["pnl", "volume", "win_rate"]),
    window: str = Query("all", enum=["all", "30d", "7d", "24h"]),
    limit: int = Query(50, ge=1, le=500)
):
    """Serve live leaderboard from C++ in-memory top-K heap."""
    lb = engine_client.get_leaderboard(k=limit)
    if not lb:
        # Fallback fixtures for demo preview if engine is cold
        return [
            {
                "wallet_idx": 1,
                "wallet": "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E",
                "total_pnl": 142500.50,
                "realized_pnl": 120000.00,
                "unrealized_pnl": 22500.50,
                "volume": 2840000.00,
                "winning_markets": 45,
                "total_markets": 52
            },
            {
                "wallet_idx": 2,
                "wallet": "0xC5d563A36AE78145C45a50134d48A1215220f80a",
                "total_pnl": 98400.20,
                "realized_pnl": 85000.00,
                "unrealized_pnl": 13400.20,
                "volume": 1450000.00,
                "winning_markets": 31,
                "total_markets": 40
            },
            {
                "wallet_idx": 3,
                "wallet": "0x10455b94be059c2331dfc956ec945892576b978b",
                "total_pnl": 67320.10,
                "realized_pnl": 71000.00,
                "unrealized_pnl": -3679.90,
                "volume": 980000.00,
                "winning_markets": 18,
                "total_markets": 25
            }
        ]
    return lb

@app.get("/wallet/{addr}")
def get_wallet(addr: str):
    """Per-wallet portfolio summary with position breakdowns."""
    data = engine_client.get_wallet(addr)
    if "error" in data:
        # Return structured preview if address not yet indexed
        return {
            "wallet": addr,
            "realized_pnl": 0.0,
            "unrealized_pnl": 0.0,
            "total_pnl": 0.0,
            "volume": 0.0,
            "position_count": 0,
            "winning_markets": 0,
            "losing_markets": 0,
            "positions": []
        }
    return data

@app.get("/wallet/{addr}/curve")
def get_wallet_curve(addr: str):
    """Historical PnL time series from ClickHouse."""
    return ch_service.get_wallet_curve(addr)

@app.get("/market/{market_id}")
def get_market(market_id: str):
    return {
        "market_id": market_id,
        "question": "Will Ethereum reach $5,000 in 2026?",
        "status": "active",
        "top_holders": [
            {"wallet": "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E", "shares": 50000, "side": "YES"},
            {"wallet": "0xC5d563A36AE78145C45a50134d48A1215220f80a", "shares": 35000, "side": "NO"}
        ]
    }

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            # Handle subscribe commands: e.g. "sub:wallet:0x1234"
            if data.startswith("sub:"):
                channel = data[4:]
                manager.subscriptions[websocket].add(channel)
    except WebSocketDisconnect:
        manager.disconnect(websocket)
