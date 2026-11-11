import json
import os
import tempfile
import time
from dataclasses import asdict, dataclass


@dataclass
class Checkpoint:
    last_block: int
    last_log_index: int
    block_hash: str
    timestamp: float

    @classmethod
    def empty(cls) -> "Checkpoint":
        return cls(last_block=0, last_log_index=0, block_hash="", timestamp=time.time())


class CheckpointManager:
    def __init__(self, filepath: str) -> None:
        self.filepath = filepath
        os.makedirs(os.path.dirname(os.path.abspath(filepath)), exist_ok=True)

    def load(self) -> Checkpoint:
        if not os.path.exists(self.filepath):
            return Checkpoint.empty()
        try:
            with open(self.filepath, encoding="utf-8") as f:
                data = json.load(f)
                return Checkpoint(
                    last_block=int(data.get("last_block", 0)),
                    last_log_index=int(data.get("last_log_index", 0)),
                    block_hash=str(data.get("block_hash", "")),
                    timestamp=float(data.get("timestamp", 0.0)),
                )
        except Exception:
            return Checkpoint.empty()

    def save(self, cp: Checkpoint) -> None:
        """Atomic write via temporary file and atomic rename."""
        dir_name = os.path.dirname(os.path.abspath(self.filepath))
        with tempfile.NamedTemporaryFile("w", dir=dir_name, delete=False, encoding="utf-8") as tmp:
            json.dump(asdict(cp), tmp, indent=2)
            temp_path = tmp.name

        # On Windows and POSIX, os.replace is atomic
        os.replace(temp_path, self.filepath)
