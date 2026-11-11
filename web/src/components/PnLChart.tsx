"use client";

import React from "react";

interface PnLPoint {
  timestamp: string | number;
  total_pnl: number;
}

interface PnLChartProps {
  points?: PnLPoint[];
}

export function PnLChart({ points = [] }: PnLChartProps) {
  // Use default curve if no points
  const data = points.length > 0 ? points : [
    { timestamp: "10-Nov", total_pnl: 0 },
    { timestamp: "10-Nov 06:00", total_pnl: 1250 },
    { timestamp: "10-Nov 12:00", total_pnl: 3400 },
    { timestamp: "10-Nov 18:00", total_pnl: 2800 },
    { timestamp: "11-Nov 00:00", total_pnl: 8900 },
    { timestamp: "11-Nov 06:00", total_pnl: 14200 },
  ];

  const values = data.map((d) => d.total_pnl);
  const minVal = Math.min(...values, 0);
  const maxVal = Math.max(...values, 100);
  const range = maxVal - minVal || 1;

  const width = 800;
  const height = 240;
  const padding = 30;

  const getX = (index: number) => padding + (index / (data.length - 1)) * (width - 2 * padding);
  const getY = (val: number) => height - padding - ((val - minVal) / range) * (height - 2 * padding);

  const pathD = data.reduce((acc, curr, idx) => {
    const x = getX(idx);
    const y = getY(curr.total_pnl);
    return idx === 0 ? `M ${x} ${y}` : `${acc} L ${x} ${y}`;
  }, "");

  const areaD = `${pathD} L ${getX(data.length - 1)} ${height - padding} L ${getX(0)} ${height - padding} Z`;

  return (
    <div className="bg-card p-4 rounded-lg border border-border">
      <div className="flex justify-between items-center mb-4">
        <h3 className="text-xs font-semibold text-zinc-400 uppercase tracking-wider">
          Cumulative PnL Curve (ClickHouse Analytics)
        </h3>
        <span className="text-xs font-mono text-success font-medium">
          +${(data[data.length - 1].total_pnl).toLocaleString()} Net
        </span>
      </div>

      <div className="relative w-full overflow-hidden">
        <svg viewBox={`0 0 ${width} ${height}`} className="w-full h-48 overflow-visible">
          <defs>
            <linearGradient id="pnlGradient" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#10b981" stopOpacity="0.25" />
              <stop offset="100%" stopColor="#10b981" stopOpacity="0.0" />
            </linearGradient>
          </defs>

          {/* Grid lines */}
          <line
            x1={padding}
            y1={getY(0)}
            x2={width - padding}
            y2={getY(0)}
            stroke="#27272a"
            strokeDasharray="4 4"
          />

          {/* Fill area */}
          <path d={areaD} fill="url(#pnlGradient)" />

          {/* Line */}
          <path d={pathD} fill="none" stroke="#10b981" strokeWidth="2" strokeLinecap="round" />

          {/* Data point dots */}
          {data.map((d, i) => (
            <circle
              key={i}
              cx={getX(i)}
              cy={getY(d.total_pnl)}
              r="3.5"
              fill="#10b981"
              className="hover:r-5 transition-all cursor-pointer"
            />
          ))}
        </svg>
      </div>
    </div>
  );
}
