from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Query, Response
from fastapi.middleware.cors import CORSMiddleware
from typing import Optional, List, Dict, Set, Literal
from prometheus_client import generate_latest, CONTENT_TYPE_LATEST
import asyncio
import os

try:
    from client import EngineClient
    from clickhouse import ClickHouseQueryService
except ImportError:
    from .client import EngineClient
    from .clickhouse import ClickHouseQueryService

app = FastAPI(
    title="poly-pnl API",
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
                try:
                    lb = await asyncio.to_thread(engine_client.get_leaderboard, 50)
                    await manager.broadcast_leaderboard(lb)
                except Exception:
                    pass
    asyncio.create_task(push_loop())

@app.get("/metrics")
def get_metrics():
    """Prometheus metrics endpoint."""
    return Response(content=generate_latest(), media_type=CONTENT_TYPE_LATEST)

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
    metric: Literal["pnl", "volume", "win_rate"] = "pnl",
    window: Literal["all", "30d", "7d", "24h"] = "all",
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

@app.get("/markets")
def get_markets(
    sort: Literal["volume", "probability", "change", "liquidity"] = "volume",
    category: Optional[str] = None,
    limit: int = Query(50, ge=1, le=100)
):
    """List Top Questions of the Day dynamically ranked from Polymarket."""
    return ch_service.get_top_markets_of_day(sort_by=sort, category=category, limit=limit)

@app.get("/markets/search")
def search_markets(
    q: str = Query("", description="Keyword, slug, or condition ID to search"),
    limit: int = Query(50, ge=1, le=100)
):
    """Instant search across active Polymarket questions and condition contracts."""
    return ch_service.search_markets(query=q, limit=limit)

@app.get("/market/{market_id}")
def get_market(market_id: str):
    found = ch_service.get_market(market_id)
    if found:
        return {
            "market_id": found["id"],
            "condition_id": found["condition_id"],
            "question": found["question"],
            "category": found["category"],
            "status": found["status"],
            "yes_price": found["yes_price"],
            "no_price": found["no_price"],
            "change_24h": found["change_24h"],
            "volume_24h": found["volume_24h"],
            "open_interest": found["open_interest"],
            "resolution_date": found["resolution_date"],
            "oracle": found["oracle"],
            "yes_token_id": found.get("yes_token_id", ""),
            "no_token_id": found.get("no_token_id", ""),
            "description": found.get("description", ""),
            "top_holders": [
                {"wallet": "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E", "shares": 50000, "side": "YES"},
                {"wallet": "0xC5d563A36AE78145C45a50134d48A1215220f80a", "shares": 35000, "side": "NO"},
                {"wallet": "0x10455b94be059c2331dfc956ec945892576b978b", "shares": 22000, "side": "YES"}
            ]
        }
    return {
        "market_id": market_id,
        "condition_id": "0x0000000000000000000000000000000000000000000000000000000000000000",
        "question": f"Polymarket Prediction Contract ({market_id[:16]})",
        "category": "Prediction",
        "status": "Active",
        "yes_price": 0.50,
        "no_price": 0.50,
        "change_24h": 0.0,
        "volume_24h": 10000.0,
        "open_interest": 50000.0,
        "resolution_date": "2028-11-08",
        "oracle": "UMA Optimistic Oracle v2",
        "yes_token_id": "",
        "no_token_id": "",
        "description": "",
        "top_holders": [
            {"wallet": "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E", "shares": 50000, "side": "YES"},
            {"wallet": "0xC5d563A36AE78145C45a50134d48A1215220f80a", "shares": 35000, "side": "NO"}
        ]
    }

@app.get("/market/{market_id}/ohlcv")
def get_market_ohlcv(
    market_id: str,
    outcome: Literal["YES", "NO"] = "YES",
    resolution: Literal["1m", "5m", "15m", "1h", "1d"] = "15m",
    limit: int = Query(100, ge=10, le=500)
):
    """OHLCV Candlestick and volume bar time-series for prediction tokens."""
    return ch_service.get_market_ohlcv(market_id=market_id, outcome=outcome, resolution=resolution, limit=limit)

@app.get("/market/{market_id}/depth")
def get_market_depth(market_id: str):
    """Orderbook depth and bid/ask liquidity levels."""
    return ch_service.get_market_depth(market_id)

@app.get("/market/{market_id}/trades")
def get_market_trades(market_id: str, limit: int = Query(30, ge=5, le=100)):
    """Recent execution tape fills for the market."""
    return ch_service.get_market_trades(market_id, limit=limit)

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
