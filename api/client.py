import socket
import json
from typing import Dict, Any, List

class EngineClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 9998) -> None:
        self.host = host
        self.port = port

    def _query(self, cmd: str) -> str:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2.0)
            s.connect((self.host, self.port))
            s.sendall(cmd.encode("utf-8") + b"\n")
            chunks = []
            while True:
                data = s.recv(4096)
                if not data:
                    break
                chunks.append(data)
            return b"".join(chunks).decode("utf-8")

    def ping(self) -> bool:
        try:
            return self._query("PING").strip() == "PONG"
        except Exception:
            return False

    def get_stats(self) -> Dict[str, Any]:
        try:
            res = self._query("STATS")
            return json.loads(res)
        except Exception:
            return {
                "events_processed": 0,
                "fills_processed": 0,
                "rollbacks_applied": 0,
                "last_block": 0,
                "incomplete_history_events": 0,
                "status": "engine_disconnected"
            }

    def get_leaderboard(self, k: int = 100) -> List[Dict[str, Any]]:
        try:
            res = self._query(f"LEADERBOARD {k}")
            return json.loads(res)
        except Exception:
            return []

    def get_wallet(self, addr: str) -> Dict[str, Any]:
        try:
            res = self._query(f"WALLET {addr}")
            return json.loads(res)
        except Exception:
            return {"error": "wallet not found or engine offline"}
