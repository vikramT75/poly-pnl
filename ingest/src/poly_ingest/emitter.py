import asyncio
import struct
from dataclasses import dataclass


@dataclass
class RawLogItem:
    block_number: int
    timestamp_s: int
    log_index: int
    tx_index: int
    address: bytes  # 20 bytes
    topics: list[bytes]  # up to 4 topics, each 32 bytes
    data: bytes  # variable length ABI data
    block_hash: str = ""


RAWLOG_STRUCT = struct.Struct("<QQII20s128sB3sI1024s4s")
assert RAWLOG_STRUCT.size == 1208, f"Expected 1208 bytes, got {RAWLOG_STRUCT.size}"


def encode_raw_log(item: RawLogItem) -> bytes:
    # 20 bytes address
    addr_bytes = item.address[:20].ljust(20, b"\x00")

    # 128 bytes topics (up to 4 x 32 bytes)
    topics_buf = bytearray(128)
    num_topics = min(len(item.topics), 4)
    for i in range(num_topics):
        t = item.topics[i][:32].ljust(32, b"\x00")
        topics_buf[i * 32 : (i + 1) * 32] = t

    # data length and data buffer
    data_len = min(len(item.data), 1024)
    data_buf = item.data[:1024].ljust(1024, b"\x00")

    payload = RAWLOG_STRUCT.pack(
        item.block_number,
        item.timestamp_s,
        item.log_index,
        item.tx_index,
        addr_bytes,
        bytes(topics_buf),
        num_topics,
        b"\x00\x00\x00",
        data_len,
        data_buf,
        b"\x00\x00\x00\x00",
    )
    # Length-prefixed frame: 4 bytes LE uint32 length + payload
    frame_len = len(payload)
    return struct.pack("<I", frame_len) + payload


def encode_rollback(rollback_block: int) -> bytes:
    """Encode rollback command: [4-byte len=12][4-byte 'ROLL'][8-byte uint64 rollback_block]"""
    payload = b"ROLL" + struct.pack("<Q", rollback_block)
    return struct.pack("<I", len(payload)) + payload


def encode_mark(token_id: str, mark_price: int) -> bytes:
    """Encode mark command: [4-byte len=44][4-byte 'MARK'][32-byte token_id][8-byte int64 mark_price]"""
    token_str = str(token_id).strip()
    if token_str.startswith(("0x", "0X")):
        raw_int = int(token_str, 16)
    else:
        raw_int = int(token_str)
    token_bytes = raw_int.to_bytes(32, byteorder="big")
    payload = b"MARK" + token_bytes + struct.pack("<q", mark_price)
    return struct.pack("<I", len(payload)) + payload


class EngineEmitter:
    def __init__(
        self, host: str = "127.0.0.1", port: int = 9999, socket_path: str | None = None
    ) -> None:
        self.host = host
        self.port = port
        self.socket_path = socket_path
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None

    async def connect(self) -> None:
        while True:
            try:
                self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
                break
            except Exception:
                await asyncio.sleep(1.0)

    async def emit_log(self, item: RawLogItem) -> None:
        frame = encode_raw_log(item)
        if not self.writer:
            await self.connect()
        try:
            assert self.writer is not None
            self.writer.write(frame)
            await self.writer.drain()
        except Exception:
            await self.connect()
            assert self.writer is not None
            self.writer.write(frame)
            await self.writer.drain()

    async def emit_batch(self, items: list[RawLogItem]) -> None:
        if not items:
            return
        buf = bytearray()
        for item in items:
            buf.extend(encode_raw_log(item))
        if not self.writer:
            await self.connect()
        try:
            assert self.writer is not None
            self.writer.write(buf)
            await self.writer.drain()
        except Exception:
            await self.connect()
            assert self.writer is not None
            self.writer.write(buf)
            await self.writer.drain()

    async def emit_rollback(self, rollback_block: int) -> None:
        frame = encode_rollback(rollback_block)
        if not self.writer:
            await self.connect()
        assert self.writer is not None
        self.writer.write(frame)
        await self.writer.drain()

    async def emit_mark(self, token_id: str, mark_price: int) -> None:
        try:
            frame = encode_mark(token_id, mark_price)
        except Exception:
            return
        if not self.writer:
            await self.connect()
        try:
            assert self.writer is not None
            self.writer.write(frame)
            await self.writer.drain()
        except Exception:
            await self.connect()
            if self.writer:
                self.writer.write(frame)
                await self.writer.drain()

    async def close(self) -> None:
        if self.writer:
            self.writer.close()
            await self.writer.wait_closed()
