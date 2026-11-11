"use client";

import React, { useState, useEffect } from "react";
import Link from "next/link";

interface LeaderboardEntry {
  wallet_idx: number;
  wallet: string;
  total_pnl: number;
  realized_pnl: number;
  unrealized_pnl: number;
  volume: number;
  winning_markets: number;
  total_markets: number;
}

export function LeaderboardTable() {
  const [data, setData] = useState<LeaderboardEntry[]>([
    {
      wallet_idx: 1,
      wallet: "0x4bfb41d5b3570defd03c39a9a4d8de6bd8b8982e",
      total_pnl: 142500.5,
      realized_pnl: 120000.0,
      unrealized_pnl: 22500.5,
      volume: 2840000.0,
      winning_markets: 45,
      total_markets: 52,
    },
    {
      wallet_idx: 2,
      wallet: "0xc5d563a36ae78145c45a50134d48a1215220f80a",
      total_pnl: 98400.2,
      realized_pnl: 85000.0,
      unrealized_pnl: 13400.2,
      volume: 1450000.0,
      winning_markets: 31,
      total_markets: 40,
    },
    {
      wallet_idx: 3,
      wallet: "0x10455b94be059c2331dfc956ec945892576b978b",
      total_pnl: 67320.1,
      realized_pnl: 71000.0,
      unrealized_pnl: -3679.9,
      volume: 980000.0,
      winning_markets: 18,
      total_markets: 25,
    },
    {
      wallet_idx: 4,
      wallet: "0x3f5666f58af0ea43527b138e932c0276d49863c0",
      total_pnl: 45210.8,
      realized_pnl: 39000.0,
      unrealized_pnl: 6210.8,
      volume: 670000.0,
      winning_markets: 14,
      total_markets: 19,
    },
    {
      wallet_idx: 5,
      wallet: "0xada100874d00e3331d00f2007a9c336a65009718",
      total_pnl: 28900.0,
      realized_pnl: 32000.0,
      unrealized_pnl: -3100.0,
      volume: 490000.0,
      winning_markets: 12,
      total_markets: 18,
    },
  ]);
  const [filter, setFilter] = useState<"pnl" | "volume" | "win_rate">("pnl");

  useEffect(() => {
    // Try live WebSocket
    let ws: WebSocket | null = null;
    try {
      ws = new WebSocket("ws://localhost:8080/ws");
      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          if (msg.type === "leaderboard" && Array.isArray(msg.data) && msg.data.length > 0) {
            setData(msg.data);
          }
        } catch {}
      };
    } catch {}

    return () => {
      if (ws) ws.close();
    };
  }, []);

  const sortedData = [...data].sort((a, b) => {
    if (filter === "volume") return b.volume - a.volume;
    if (filter === "win_rate") {
      const wrA = a.total_markets ? a.winning_markets / a.total_markets : 0;
      const wrB = b.total_markets ? b.winning_markets / b.total_markets : 0;
      return wrB - wrA;
    }
    return b.total_pnl - a.total_pnl;
  });

  return (
    <div className="bg-card rounded-lg border border-border overflow-hidden">
      <div className="p-4 border-b border-border flex items-center justify-between">
        <div>
          <h2 className="text-base font-semibold text-white">POLYMARKET TOP TRADERS LEADERBOARD</h2>
          <p className="text-xs text-zinc-500 mt-0.5">Real-time incremental PnL tracking across all active Polygon wallets</p>
        </div>
        <div className="flex gap-2">
          {(["pnl", "volume", "win_rate"] as const).map((f) => (
            <button
              key={f}
              onClick={() => setFilter(f)}
              className={`px-3 py-1 text-xs rounded uppercase font-medium transition-colors ${
                filter === f
                  ? "bg-accent text-white"
                  : "bg-zinc-800 text-zinc-400 hover:bg-zinc-700"
              }`}
            >
              {f.replace("_", " ")}
            </button>
          ))}
        </div>
      </div>

      <div className="overflow-x-auto">
        <table className="w-full text-left text-xs">
          <thead className="bg-zinc-900/50 text-zinc-400 border-b border-border">
            <tr>
              <th className="py-2.5 px-4 font-medium">#</th>
              <th className="py-2.5 px-4 font-medium">WALLET ADDRESS</th>
              <th className="py-2.5 px-4 font-medium text-right">TOTAL PnL</th>
              <th className="py-2.5 px-4 font-medium text-right">REALIZED</th>
              <th className="py-2.5 px-4 font-medium text-right">UNREALIZED</th>
              <th className="py-2.5 px-4 font-medium text-right">VOLUME</th>
              <th className="py-2.5 px-4 font-medium text-right">WIN RATE</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-border/60">
            {sortedData.map((row, index) => {
              const winRate = row.total_markets
                ? Math.round((row.winning_markets / row.total_markets) * 100)
                : 0;
              return (
                <tr
                  key={row.wallet}
                  className="hover:bg-zinc-800/40 transition-colors group"
                >
                  <td className="py-3 px-4 font-mono text-zinc-500">{index + 1}</td>
                  <td className="py-3 px-4">
                    <Link
                      href={`/wallet/${row.wallet}`}
                      className="text-zinc-300 group-hover:text-accent font-mono transition-colors"
                    >
                      {row.wallet.slice(0, 8)}...{row.wallet.slice(-6)}
                    </Link>
                  </td>
                  <td className={`py-3 px-4 text-right font-medium font-mono ${
                    row.total_pnl >= 0 ? "text-success" : "text-danger"
                  }`}>
                    {row.total_pnl >= 0 ? "+" : ""}${row.total_pnl.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
                  </td>
                  <td className="py-3 px-4 text-right font-mono text-zinc-300">
                    ${row.realized_pnl.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
                  </td>
                  <td className={`py-3 px-4 text-right font-mono ${
                    row.unrealized_pnl >= 0 ? "text-zinc-300" : "text-danger"
                  }`}>
                    {row.unrealized_pnl >= 0 ? "+" : ""}${row.unrealized_pnl.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
                  </td>
                  <td className="py-3 px-4 text-right font-mono text-zinc-400">
                    ${row.volume.toLocaleString(undefined, { minimumFractionDigits: 0 })}
                  </td>
                  <td className="py-3 px-4 text-right font-mono">
                    <span className="px-2 py-0.5 rounded bg-zinc-800 border border-zinc-700/50 text-zinc-200">
                      {winRate}% ({row.winning_markets}/{row.total_markets})
                    </span>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}
