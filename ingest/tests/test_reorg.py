from poly_ingest.reorg import BlockHeader, ReorgTracker


def test_sequential_blocks_no_reorg():
    tracker = ReorgTracker(window_size=10)

    # Blocks 100, 101, 102 sequentially
    b1 = BlockHeader(number=100, block_hash="0x100", parent_hash="0x099")
    b2 = BlockHeader(number=101, block_hash="0x101", parent_hash="0x100")
    b3 = BlockHeader(number=102, block_hash="0x102", parent_hash="0x101")

    assert tracker.add_block(b1) == []
    assert tracker.add_block(b2) == []
    assert tracker.add_block(b3) == []
    assert tracker.get_tip().number == 102


def test_same_block_no_reorg():
    tracker = ReorgTracker(window_size=10)
    b1 = BlockHeader(number=100, block_hash="0x100")
    assert tracker.add_block(b1) == []
    # Multiple logs in the same block height
    b1_dup = BlockHeader(number=100, block_hash="0x100")
    assert tracker.add_block(b1_dup) == []
    assert tracker.get_tip().number == 100


def test_sparse_blocks_forward_no_reorg():
    tracker = ReorgTracker(window_size=10)
    # Blocks with gaps (no trading events in between)
    assert tracker.add_block(BlockHeader(number=100, block_hash="0x100")) == []
    assert tracker.add_block(BlockHeader(number=105, block_hash="0x105")) == []
    assert tracker.get_tip().number == 105


def test_reorg_fork_detection():
    tracker = ReorgTracker(window_size=10)

    b1 = BlockHeader(number=100, block_hash="0x100", parent_hash="0x099")
    b2 = BlockHeader(number=101, block_hash="0x101_canonical", parent_hash="0x100")
    b3 = BlockHeader(number=102, block_hash="0x102_canonical", parent_hash="0x101_canonical")

    tracker.add_block(b1)
    tracker.add_block(b2)
    tracker.add_block(b3)

    # Reorg happens: block 102 is replaced by alternative block 102 branching off block 101
    b2_alt = BlockHeader(number=101, block_hash="0x101_alt", parent_hash="0x100")

    # When 101_alt arrives (parent is 100), it should pop 102 and 101
    rolled_back = tracker.add_block(b2_alt)
    assert 102 in rolled_back
    assert 101 in rolled_back
    assert tracker.get_tip().block_hash == "0x101_alt"
