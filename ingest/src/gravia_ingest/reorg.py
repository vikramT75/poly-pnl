from collections import deque
from dataclasses import dataclass
from typing import List, Optional, Tuple

@dataclass
class BlockHeader:
    number: int
    block_hash: str
    parent_hash: str

class ReorgTracker:
    def __init__(self, window_size: int = 128) -> None:
        self.window_size = window_size
        self.chain: deque[BlockHeader] = deque(maxlen=window_size)

    def add_block(self, block: BlockHeader) -> List[int]:
        """
        Add a new block to the canonical chain.
        Returns a list of block numbers that need to be rolled back if a fork/reorg is detected.
        """
        if not self.chain:
            self.chain.append(block)
            return []

        tip = self.chain[-1]

        # Normal sequential progression
        if block.number == tip.number + 1 and block.parent_hash.lower() == tip.block_hash.lower():
            self.chain.append(block)
            return []

        # Reorg or gap detection
        # Case A: Same block height with different hash, or lower block height
        rolled_back_blocks: List[int] = []

        # Find the common ancestor in our window
        found_ancestor = False
        while self.chain:
            curr_tip = self.chain[-1]
            if curr_tip.number < block.number and curr_tip.block_hash.lower() == block.parent_hash.lower():
                found_ancestor = True
                break
            # Pop reorged block
            reorged = self.chain.pop()
            rolled_back_blocks.append(reorged.number)

        self.chain.append(block)
        return rolled_back_blocks

    def get_tip(self) -> Optional[BlockHeader]:
        return self.chain[-1] if self.chain else None

    def contains_block(self, number: int) -> bool:
        return any(b.number == number for b in self.chain)
