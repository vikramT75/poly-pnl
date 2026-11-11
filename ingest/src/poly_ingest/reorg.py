from collections import deque
from dataclasses import dataclass


@dataclass
class BlockHeader:
    number: int
    block_hash: str = ""
    parent_hash: str = ""


class ReorgTracker:
    def __init__(self, window_size: int = 128) -> None:
        self.window_size = window_size
        self.chain: deque[BlockHeader] = deque(maxlen=window_size)
        self.by_number: dict[int, BlockHeader] = {}

    def add_block(self, block: BlockHeader) -> list[int]:
        """
        Add a new block to the canonical chain.
        Returns a list of block numbers that need to be rolled back if a fork/reorg is detected.
        """
        if not self.chain:
            self.chain.append(block)
            self.by_number[block.number] = block
            return []

        tip = self.chain[-1]

        # Case 1: Same block height (multiple logs in the same block)
        if block.number == tip.number:
            # If same hash (or hashes not provided), it's the same canonical block -> no reorg
            if (
                not block.block_hash
                or not tip.block_hash
                or block.block_hash.lower() == tip.block_hash.lower()
            ):
                return []
            # Conflicting hash at the same block height -> single-block reorg
            self.chain.pop()
            self.by_number.pop(tip.number, None)
            self.chain.append(block)
            self.by_number[block.number] = block
            return [tip.number]

        # Case 2: Advancing forward in height
        if block.number > tip.number:
            # If parent_hash is known and conflicts with our tip's hash
            if (
                block.number == tip.number + 1
                and block.parent_hash
                and tip.block_hash
                and block.parent_hash.lower() != tip.block_hash.lower()
            ):
                rolled_back = []
                while self.chain:
                    curr = self.chain[-1]
                    if block.parent_hash and curr.block_hash.lower() == block.parent_hash.lower():
                        break
                    p = self.chain.pop()
                    self.by_number.pop(p.number, None)
                    rolled_back.append(p.number)
                self.chain.append(block)
                self.by_number[block.number] = block
                return rolled_back

            # Normal forward progression
            self.chain.append(block)
            self.by_number[block.number] = block
            return []

        # Case 3: Block number is strictly less than tip.number (block.number < tip.number)
        # Check if this block matches our existing recorded block at this height
        if block.number in self.by_number:
            existing = self.by_number[block.number]
            if (
                block.block_hash
                and existing.block_hash
                and block.block_hash.lower() == existing.block_hash.lower()
            ):
                return []

        # Deep reorg: rollback chain back to block.number
        rolled_back = []
        while self.chain and self.chain[-1].number >= block.number:
            p = self.chain.pop()
            self.by_number.pop(p.number, None)
            rolled_back.append(p.number)

        self.chain.append(block)
        self.by_number[block.number] = block
        return rolled_back

    def get_tip(self) -> BlockHeader | None:
        return self.chain[-1] if self.chain else None

    def contains_block(self, number: int) -> bool:
        return number in self.by_number or any(b.number == number for b in self.chain)
