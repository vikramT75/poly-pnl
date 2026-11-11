"use client";

import React, { useState } from "react";

export interface DailyPnLEntry {
  date: string;
  pnl: number;
  trades_count: number;
}

interface DailyPnLBarsProps {
  data?: DailyPnLEntry[];
}

export function DailyPnLBars({ data = [] }: DailyPnLBarsProps) {
  // Default mock daily returns if no specific history provided
  const days: DailyPnLEntry[] = data.length > 0 ? data : [
    { date: "05-Nov", pnl: 450.0, trades_count: 14 },
    { date: "06-Nov", pnl: -220.0, trades_count: 8 },
    { date: "07-Nov", pnl: 1840.5, trades_count: 22 },
    { date: "08-Nov", pnl: 920.0, trades_count: 15 },
    { date: "09-Nov", pnl: -610.0, trades_count: 11 },
    { date: "10-Nov", pnl: 3120.0, trades_count: 31 },
    { date: "11-Nov", pnl: 1350.0, trades_count: 19 },
  ];

  const [hovered, setHovered] = useState<DailyPnLEntry | null>(null);

  const values = days.map((d) => d.pnl);
  const maxVal = Math.max(...values.map(Math.abs), 100);

  return (
    <div className="bg-card p-4 rounded-lg border border-border flex flex-col font-mono text-xs">
      <div className="flex justify-between items-center mb-3">
        <div>
          <h3 className="text-xs font-semibold text-zinc-300 uppercase tracking-wider">
            Daily PnL Performance Bars
          </h3>
          <span className="text-[10px] text-zinc-500">Realized + Mark-to-market daily returns</span>
        </div>
        <div className="text-right">
          {hovered ? (
            <div>
              <span className="text-zinc-400 mr-2">{hovered.date}:</span>
              <span className={`font-bold ${hovered.pnl >= 0 ? "text-success" : "text-danger"}`}>
                {hovered.pnl >= 0 ? "+" : ""}${hovered.pnl.toLocaleString(undefined, { minimumFractionDigits: 2 })}
              </span>
              <span className="text-zinc-500 text-[10px] ml-2">({hovered.trades_count} fills)</span>
            </div>
          ) : (
            <span className="text-[11px] text-zinc-400">
              Hover bar for breakdown
            </span>
          )}
        </div>
      </div>

      {/* Bar Chart Container */}
      <div className="h-32 flex items-end gap-2 pt-4 pb-2 border-b border-border/60">
        {days.map((item, idx) => {
          const isPos = item.pnl >= 0;
          const heightPct = Math.min(100, Math.max(10, (Math.abs(item.pnl) / maxVal) * 85));

          return (
            <div
              key={idx}
              onMouseEnter={() => setHovered(item)}
              onMouseLeave={() => setHovered(null)}
              className="flex-1 flex flex-col justify-end items-center h-full cursor-pointer group"
            >
              <div
                className={`w-full rounded-t transition-all duration-200 group-hover:brightness-125 ${
                  isPos
                    ? "bg-emerald-500/80 border-t-2 border-emerald-400 shadow-[0_0_10px_rgba(16,185,129,0.2)]"
                    : "bg-rose-500/80 border-t-2 border-rose-400 shadow-[0_0_10px_rgba(239,68,68,0.2)]"
                }`}
                style={{ height: `${heightPct}%` }}
              />
              <span className="text-[10px] text-zinc-500 mt-2 truncate max-w-full group-hover:text-zinc-200">
                {item.date}
              </span>
            </div>
          );
        })}
      </div>

      <div className="flex justify-between items-center mt-2 text-[10px] text-zinc-500">
        <span>Baseline: $0.00</span>
        <span className="text-success font-medium">
          7-Day Win Rate: {Math.round((days.filter((d) => d.pnl > 0).length / days.length) * 100)}%
        </span>
      </div>
    </div>
  );
}
