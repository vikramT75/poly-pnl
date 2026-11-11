"use client";

import React from "react";

export interface DepthLevel {
  price: number;
  size: number;
  total: number;
}

interface OrderBookDepthProps {
  bids: DepthLevel[];
  asks: DepthLevel[];
  midpoint: number;
  spread: number;
  impliedProbability: string;
}

export function OrderBookDepth({
  bids = [],
  asks = [],
  midpoint = 0.5,
  spread = 0.02,
  impliedProbability = "50.0%",
}: OrderBookDepthProps) {
  // Max cumulative depth for bar width scaling
  const maxBidTotal = bids.length > 0 ? bids[bids.length - 1].total : 1;
  const maxAskTotal = asks.length > 0 ? asks[asks.length - 1].total : 1;
  const maxTotal = Math.max(maxBidTotal, maxAskTotal, 1);

  return (
    <div className="bg-card rounded-lg border border-border overflow-hidden flex flex-col h-full font-mono text-xs">
      {/* Header */}
      <div className="p-3 border-b border-border bg-zinc-950/40 flex items-center justify-between">
        <div>
          <span className="font-semibold text-zinc-300 text-xs tracking-wider uppercase">
            Order Book & Depth
          </span>
          <span className="text-[10px] text-zinc-500 block">CLOB Liquidity Walls</span>
        </div>
        <div className="text-right">
          <span className="text-accent font-bold text-sm">{impliedProbability}</span>
          <span className="text-[10px] text-zinc-500 block">Implied Win Rate</span>
        </div>
      </div>

      {/* Midpoint & Spread Banner */}
      <div className="px-3 py-1.5 bg-zinc-900/60 border-b border-border/60 flex items-center justify-between text-[11px]">
        <div className="flex items-center gap-1.5">
          <span className="text-zinc-500">MID:</span>
          <span className="font-bold text-zinc-200">${midpoint.toFixed(3)}</span>
        </div>
        <div className="flex items-center gap-1.5">
          <span className="text-zinc-500">SPREAD:</span>
          <span className="text-zinc-400 font-mono">${spread.toFixed(3)} ({(spread * 100).toFixed(1)}¢)</span>
        </div>
      </div>

      {/* Column Headers */}
      <div className="grid grid-cols-3 px-3 py-1.5 text-[10px] text-zinc-500 border-b border-border/40 font-semibold uppercase">
        <span>Price</span>
        <span className="text-right">Size</span>
        <span className="text-right">Depth</span>
      </div>

      <div className="flex-1 flex flex-col justify-between overflow-y-auto">
        {/* Asks (Sells) - Rendered top to bottom (highest ask down to lowest ask) */}
        <div className="flex flex-col-reverse divide-y divide-border/20">
          {asks.slice(0, 7).map((ask, idx) => {
            const widthPct = Math.min(100, Math.max(4, (ask.total / maxTotal) * 100));
            return (
              <div key={idx} className="relative grid grid-cols-3 px-3 py-1 items-center hover:bg-zinc-800/30">
                {/* Red depth bar extending from right */}
                <div
                  className="absolute right-0 top-0 bottom-0 bg-rose-950/40 border-r-2 border-rose-500/60 pointer-events-none transition-all duration-300"
                  style={{ width: `${widthPct}%` }}
                />
                <span className="relative z-10 font-bold text-rose-400">
                  ${ask.price.toFixed(2)}
                </span>
                <span className="relative z-10 text-right text-zinc-300">
                  {ask.size.toLocaleString()}
                </span>
                <span className="relative z-10 text-right text-zinc-500 text-[10px]">
                  {Math.round(ask.total / 1000)}k
                </span>
              </div>
            );
          })}
        </div>

        {/* Current Mid Line */}
        <div className="px-3 py-1 bg-zinc-900 border-y border-border/80 flex items-center justify-between text-[11px] text-accent font-bold">
          <span>SPREAD TICK</span>
          <span>${midpoint.toFixed(2)}</span>
        </div>

        {/* Bids (Buys) */}
        <div className="divide-y divide-border/20">
          {bids.slice(0, 7).map((bid, idx) => {
            const widthPct = Math.min(100, Math.max(4, (bid.total / maxTotal) * 100));
            return (
              <div key={idx} className="relative grid grid-cols-3 px-3 py-1 items-center hover:bg-zinc-800/30">
                {/* Green depth bar extending from right */}
                <div
                  className="absolute right-0 top-0 bottom-0 bg-emerald-950/40 border-r-2 border-emerald-500/60 pointer-events-none transition-all duration-300"
                  style={{ width: `${widthPct}%` }}
                />
                <span className="relative z-10 font-bold text-emerald-400">
                  ${bid.price.toFixed(2)}
                </span>
                <span className="relative z-10 text-right text-zinc-300">
                  {bid.size.toLocaleString()}
                </span>
                <span className="relative z-10 text-right text-zinc-500 text-[10px]">
                  {Math.round(bid.total / 1000)}k
                </span>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
