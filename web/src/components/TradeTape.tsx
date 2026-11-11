"use client";

import React from "react";
import Link from "next/link";

export interface TradeItem {
  id: string;
  timestamp: number;
  time_str: string;
  side: "BUY" | "SELL";
  outcome: "YES" | "NO";
  price: number;
  size: number;
  value_usdc: number;
  wallet: string;
  tx_hash: string;
}

interface TradeTapeProps {
  trades: TradeItem[];
}

export function TradeTape({ trades = [] }: TradeTapeProps) {
  return (
    <div className="bg-card rounded-lg border border-border overflow-hidden flex flex-col font-mono text-xs">
      <div className="p-3 border-b border-border bg-zinc-950/40 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="w-2 h-2 rounded-full bg-success animate-ping" />
          <span className="font-semibold text-zinc-300 text-xs tracking-wider uppercase">
            Live Execution Feed
          </span>
          <span className="text-[10px] text-zinc-500">Polygon On-Chain Fills</span>
        </div>
        <span className="text-[10px] text-zinc-400 bg-zinc-900 px-2 py-0.5 rounded border border-border">
          {trades.length} Recent Fills
        </span>
      </div>

      <div className="overflow-x-auto">
        <table className="w-full text-left text-xs">
          <thead className="bg-zinc-900/40 text-zinc-400 border-b border-border/60 text-[10px] uppercase">
            <tr>
              <th className="py-2 px-3 font-semibold">Time</th>
              <th className="py-2 px-3 font-semibold">Contract</th>
              <th className="py-2 px-3 font-semibold">Action</th>
              <th className="py-2 px-3 font-semibold text-right">Price</th>
              <th className="py-2 px-3 font-semibold text-right">Shares</th>
              <th className="py-2 px-3 font-semibold text-right">Total USDC</th>
              <th className="py-2 px-3 font-semibold">Trader Wallet</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-border/30">
            {trades.map((t) => (
              <tr key={t.id} className="hover:bg-zinc-800/40 transition-colors">
                <td className="py-2 px-3 text-zinc-500">{t.time_str}</td>
                <td className="py-2 px-3">
                  <span
                    className={`px-1.5 py-0.5 rounded text-[10px] font-bold ${
                      t.outcome === "YES"
                        ? "bg-emerald-950/80 text-emerald-400 border border-emerald-800/40"
                        : "bg-rose-950/80 text-rose-400 border border-rose-800/40"
                    }`}
                  >
                    {t.outcome}
                  </span>
                </td>
                <td className="py-2 px-3">
                  <span
                    className={`font-semibold ${
                      t.side === "BUY" ? "text-emerald-400" : "text-rose-400"
                    }`}
                  >
                    {t.side}
                  </span>
                </td>
                <td className="py-2 px-3 text-right font-bold text-zinc-200">
                  ${t.price.toFixed(3)}
                </td>
                <td className="py-2 px-3 text-right text-zinc-300">
                  {t.size.toLocaleString()}
                </td>
                <td className="py-2 px-3 text-right text-accent font-semibold">
                  ${t.value_usdc.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
                </td>
                <td className="py-2 px-3">
                  <Link
                    href={`/wallet/${t.wallet}`}
                    className="text-zinc-400 hover:text-accent underline transition-colors"
                  >
                    {t.wallet.slice(0, 6)}...{t.wallet.slice(-4)}
                  </Link>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
