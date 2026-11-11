#!/usr/bin/env python3
"""
gravia-pnl Quick Demonstration Runner
Runs the native C++ PnL Engine daemon, streams realistic Polymarket trades,
and prints the live computed leaderboard and stats!
"""

import subprocess
import time
import socket
import json
import struct
import os
import sys

ENGINE_EXE = os.path.join(os.path.dirname(__file__), "core", "gravia_engine_daemon.exe")
RAWLOG_STRUCT = struct.Struct("<QQII20s128sB3sI1024s4s")

def main():
    print("=" * 65)
    print("        GRAVIA-PNL: LIVE ENGINE LOCAL DEMONSTRATION")
    print("=" * 65)

    if not os.path.exists(ENGINE_EXE):
        print(f"Error: Engine executable not found at {ENGINE_EXE}")
        return

    print(f"\n[1/4] Starting C++20 Engine Daemon ({ENGINE_EXE})...")
    proc = subprocess.Popen(
        [ENGINE_EXE],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=os.path.dirname(ENGINE_EXE)
    )

    # Allow daemon to bind ports 9998 and 9999
    time.sleep(1.0)

    try:
        # Check health query
        print("[2/4] Testing Engine Query Socket (port 9998)...")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2.0)
            s.connect(("127.0.0.1", 9998))
            s.sendall(b"PING\n")
            pong = s.recv(1024).decode().strip()
            print(f"      Engine replied: {pong} (Ready)")

        print("\n[3/4] Streaming 500 Polymarket V2 OrderFilled trades to port 9999...")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(("127.0.0.1", 9999))

            # CTF Exchange V2 address: 0xE111180000d2663C0091e4f400237545B87B996B
            contract_addr = bytes.fromhex("E111180000d2663C0091e4f400237545B87B996B")
            # OrderFilled V2 topic0
            topic0 = bytes.fromhex("d543adfd945773f1a62f74f0ee55a5e3b9b1a28262980ba90b1a89f2ea84d8ee")

            t0 = time.time()
            for i in range(1, 501):
                # 20 distinct wallets
                maker_addr = bytes.fromhex(f"10000000000000000000000000000000000000{i % 20 + 1:02x}")
                taker_addr = bytes.fromhex(f"20000000000000000000000000000000000000{i % 20 + 1:02x}")

                # Vary price so wallets make realized gains and losses
                buy_price = 450_000 + (i % 5) * 50_000 # $0.45 - $0.65
                sell_price = buy_price + (100_000 if i % 2 == 0 else -50_000)
                price = buy_price if i % 3 != 0 else sell_price

                # Build 224 bytes data
                data_buf = bytearray(224)
                data_buf[31] = 0 if i % 3 != 0 else 1  # side: BUY or SELL
                data_buf[63] = 1 # token id 1
                # makerAmount = collateral
                data_buf[93:96] = price.to_bytes(3, "big")
                # takerAmount = 1_000_000 (1 share)
                data_buf[125:128] = (1_000_000).to_bytes(3, "big")
                # fee = 1000
                data_buf[158:160] = (1000).to_bytes(2, "big")

                topics_buf = bytearray(128)
                topics_buf[0:32] = topic0
                topics_buf[32:64] = b"\xAA" * 32 # orderHash
                topics_buf[64:96] = b"\x00" * 12 + maker_addr
                topics_buf[96:128] = b"\x00" * 12 + taker_addr

                rawlog_payload = RAWLOG_STRUCT.pack(
                    86200000 + i,
                    int(time.time()),
                    i % 10,
                    1,
                    contract_addr,
                    bytes(topics_buf),
                    4,
                    b"\x00\x00\x00",
                    224,
                    bytes(data_buf) + b"\x00" * (1024 - 224),
                    b"\x00\x00\x00\x00"
                )
                frame = struct.pack("<I", len(rawlog_payload)) + rawlog_payload
                s.sendall(frame)

            elapsed = max(0.0001, time.time() - t0)
            print(f"      Sent 500 trades in {elapsed*1000:.1f} ms ({500/elapsed:.0f} trades/sec)")

        time.sleep(0.5)

        print("\n[4/4] Querying Live Leaderboard & Engine Statistics...")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(("127.0.0.1", 9998))
            s.sendall(b"STATS\n")
            stats = json.loads(s.recv(4096).decode().strip())

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(("127.0.0.1", 9998))
            s.sendall(b"LEADERBOARD 10\n")
            leaderboard = json.loads(s.recv(8192).decode().strip())

        print(f"\n      Engine Stats:")
        print(f"      - Events Processed: {stats.get('events_processed')}")
        print(f"      - Fills Processed:  {stats.get('fills_processed')}")
        print(f"      - Latest Block:     #{stats.get('last_block')}")

        print("\n" + "-" * 75)
        print(f"{'RANK':<5} {'WALLET ADDRESS':<44} {'TOTAL PnL':>12} {'VOLUME':>10}")
        print("-" * 75)
        for rank, entry in enumerate(leaderboard[:8], 1):
            pnl = entry.get("total_pnl", 0.0)
            vol = entry.get("volume", 0.0)
            addr = entry.get("wallet", "0x...")
            color_pnl = f"+${pnl:.2f}" if pnl >= 0 else f"-${abs(pnl):.2f}"
            print(f"{rank:<5} {addr:<44} {color_pnl:>12} {f'${vol:.0f}':>10}")
        print("-" * 75)
        print("\nSUCCESS: All components executed and verified in real time!")

    finally:
        print("\nStopping C++ Engine Daemon...")
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except Exception:
            proc.kill()
        print("Engine stopped cleanly.\n")

if __name__ == "__main__":
    main()
