"use client";

import React from "react";
import Link from "next/link";
import { PnLChart } from "@/components/PnLChart";
import { DailyPnLBars } from "@/components/DailyPnLBars";

interface Props {
  params: {
    addr: string;
  };
}

export default function WalletPage({ params }: Props) {
  const { addr } = params;

  // Mock positions for portfolio breakdown demonstration
  const positions = [
    {
      market_id: "trump-presidential-win-2024",
      market: "Will Donald Trump win the 2024 US Presidential Election?",
      outcome: "YES",
      size: 45000,
      avg_price: 0.52,
      mark: 0.61,
      unrealized: 4050.0,
      realized: 2200.0,
      incomplete_history: false,
    },
    {
      market_id: "fed-rate-cut-50bps-sep",
      market: "Fed cuts interest rates by >= 50 bps in September?",
      outcome: "NO",
      size: 20000,
      avg_price: 0.40,
      mark: 0.48,
      unrealized: 1600.0,
      realized: 850.0,
      incomplete_history: false,
    },
    {
      market_id: "btc-100k-dec-2024",
      market: "Bitcoin exceeds $100k before December 31, 2024?",
      outcome: "YES",
      size: 15000,
      avg_price: 0.65,
      mark: 0.58,
      unrealized: -1050.0,
      realized: -400.0,
      incomplete_history: true, // flags oversell / truncation
    },
  ];

  return (
    <div className="space-y-6">
      {/* Navigation */}
      <div className="flex items-center gap-2 text-xs text-zinc-500 font-mono">
        <Link href="/" className="hover:text-zinc-300">LEADERBOARD</Link>
        <span>/</span>
        <span className="text-zinc-300 font-mono">{addr}</span>
      </div>

      {/* Wallet Headline Cards */}
      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <div className="bg-card p-4 rounded-lg border border-border">
          <span className="text-xs text-zinc-500 font-medium font-mono">TOTAL PnL</span>
          <div className="text-2xl font-bold font-mono text-success mt-1">
            +$6,850.00
          </div>
          <span className="text-xs text-zinc-500 mt-1 block font-mono">Realized + Mark-to-Market</span>
        </div>

        <div className="bg-card p-4 rounded-lg border border-border">
          <span className="text-xs text-zinc-500 font-medium font-mono">REALIZED PnL</span>
          <div className="text-2xl font-bold font-mono text-zinc-200 mt-1">
            +$2,650.00
          </div>
          <span className="text-xs text-zinc-500 mt-1 block font-mono">Fees deducted</span>
        </div>

        <div className="bg-card p-4 rounded-lg border border-border">
          <span className="text-xs text-zinc-500 font-medium font-mono">UNREALIZED PnL</span>
          <div className="text-2xl font-bold font-mono text-success mt-1">
            +$4,600.00
          </div>
          <span className="text-xs text-zinc-500 mt-1 block font-mono">CLOB midpoint marks</span>
        </div>

        <div className="bg-card p-4 rounded-lg border border-border">
          <span className="text-xs text-zinc-500 font-medium font-mono">LIFETIME VOLUME</span>
          <div className="text-2xl font-bold font-mono text-zinc-300 mt-1">
            $142,500
          </div>
          <span className="text-xs text-zinc-500 mt-1 block font-mono">Polygon on-chain collateral</span>
        </div>
      </div>

      {/* Analytics Charts Grid: Cumulative PnL Curve & Daily Return Bars */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <PnLChart />
        <DailyPnLBars />
      </div>

      {/* Open Positions Table */}
      <div className="bg-card rounded-lg border border-border overflow-hidden font-mono">
        <div className="p-4 border-b border-border flex items-center justify-between">
          <div>
            <h3 className="text-xs font-semibold text-zinc-300 uppercase tracking-wider">
              Active Prediction Market Positions
            </h3>
            <span className="text-[10px] text-zinc-500">
              Click market question to view full candlestick & order book terminal
            </span>
          </div>
          <span className="text-xs text-zinc-400 bg-zinc-900 px-2.5 py-1 rounded border border-border">
            {positions.length} Active Contracts
          </span>
        </div>
        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs">
            <thead className="bg-zinc-900/50 text-zinc-400 border-b border-border">
              <tr>
                <th className="py-2.5 px-4 font-medium">MARKET QUESTION</th>
                <th className="py-2.5 px-4 font-medium">SIDE</th>
                <th className="py-2.5 px-4 font-medium text-right">SIZE</th>
                <th className="py-2.5 px-4 font-medium text-right">AVG COST</th>
                <th className="py-2.5 px-4 font-medium text-right">CURRENT MARK</th>
                <th className="py-2.5 px-4 font-medium text-right">UNREALIZED</th>
                <th className="py-2.5 px-4 font-medium text-right">REALIZED</th>
                <th className="py-2.5 px-4 font-medium text-center">FLAGS</th>
                <th className="py-2.5 px-4 font-medium text-center">CANDLES</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-border/60">
              {positions.map((p, i) => (
                <tr key={i} className="hover:bg-zinc-800/40 transition-colors">
                  <td className="py-3 px-4 font-medium text-zinc-200 max-w-md">
                    <Link
                      href={`/market/${p.market_id}`}
                      className="hover:text-accent transition-colors block truncate"
                    >
                      {p.market}
                    </Link>
                  </td>
                  <td className="py-3 px-4">
                    <span className={`px-2 py-0.5 rounded text-[10px] font-bold ${
                      p.outcome === "YES" ? "bg-emerald-950/80 text-emerald-400 border border-emerald-800/40" : "bg-rose-950/80 text-rose-400 border border-rose-800/40"
                    }`}>
                      {p.outcome}
                    </span>
                  </td>
                  <td className="py-3 px-4 text-right font-mono text-zinc-300">
                    {p.size.toLocaleString()}
                  </td>
                  <td className="py-3 px-4 text-right font-mono text-zinc-400">
                    ${p.avg_price.toFixed(2)}
                  </td>
                  <td className="py-3 px-4 text-right font-mono text-accent">
                    ${p.mark.toFixed(2)}
                  </td>
                  <td className={`py-3 px-4 text-right font-mono ${p.unrealized >= 0 ? "text-success" : "text-danger"}`}>
                    {p.unrealized >= 0 ? "+" : ""}${p.unrealized.toFixed(2)}
                  </td>
                  <td className={`py-3 px-4 text-right font-mono ${p.realized >= 0 ? "text-zinc-300" : "text-danger"}`}>
                    ${p.realized.toFixed(2)}
                  </td>
                  <td className="py-3 px-4 text-center">
                    {p.incomplete_history ? (
                      <span className="px-1.5 py-0.5 rounded bg-amber-950/60 border border-amber-800/40 text-amber-400 text-[10px]" title="Bought before backfill window">
                        Truncated
                      </span>
                    ) : (
                      <span className="text-zinc-600 text-[10px]">—</span>
                    )}
                  </td>
                  <td className="py-3 px-4 text-center">
                    <Link
                      href={`/market/${p.market_id}`}
                      className="px-2 py-0.5 rounded bg-zinc-800 hover:bg-accent text-zinc-300 hover:text-white transition-colors text-[10px]"
                    >
                      Chart ↗
                    </Link>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
