import os
import tempfile
import time
from poly_ingest.checkpoint import CheckpointManager, Checkpoint

def test_checkpoint_roundtrip():
    with tempfile.TemporaryDirectory() as tmpdir:
        path = os.path.join(tmpdir, "checkpoint.json")
        mgr = CheckpointManager(path)

        # Initially empty
        cp0 = mgr.load()
        assert cp0.last_block == 0

        # Save and reload
        now = time.time()
        cp1 = Checkpoint(last_block=55000100, last_log_index=12, block_hash="0xabcd", timestamp=now)
        mgr.save(cp1)

        cp2 = mgr.load()
        assert cp2.last_block == 55000100
        assert cp2.last_log_index == 12
        assert cp2.block_hash == "0xabcd"
        assert abs(cp2.timestamp - now) < 0.001
