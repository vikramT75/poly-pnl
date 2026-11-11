import struct
from poly_ingest.emitter import RawLogItem, encode_raw_log, encode_rollback, RAWLOG_STRUCT

def test_raw_log_binary_layout():
    item = RawLogItem(
        block_number=86100000,
        timestamp_s=1710000000,
        log_index=5,
        tx_index=2,
        address=b'\x4b\xfb' + b'\x00'*18,
        topics=[b'\xd0\xa0' + b'\x00'*30],
        data=b'\x01\x02\x03\x04'
    )

    frame = encode_raw_log(item)
    # Check 4-byte frame length header
    frame_len = struct.unpack("<I", frame[:4])[0]
    assert frame_len == 1208
    assert len(frame) == 4 + 1208

    # Unpack payload
    b_num, ts, l_idx, t_idx, addr, topics, n_topics, pad3, d_len, data, tail_pad = RAWLOG_STRUCT.unpack(frame[4:])
    assert b_num == 86100000
    assert ts == 1710000000
    assert l_idx == 5
    assert t_idx == 2
    assert addr[:2] == b'\x4b\xfb'
    assert n_topics == 1
    assert d_len == 4
    assert data[:4] == b'\x01\x02\x03\x04'

def test_rollback_frame():
    frame = encode_rollback(86126998)
    frame_len = struct.unpack("<I", frame[:4])[0]
    assert frame_len == 12
    magic = frame[4:8]
    assert magic == b"ROLL"
    rb_block = struct.unpack("<Q", frame[8:16])[0]
    assert rb_block == 86126998
