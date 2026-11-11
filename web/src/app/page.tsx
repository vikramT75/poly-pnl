"use client";

import React, { useEffect, useState } from "react";
import Link from "next/link";
import { SystemStatus } from "@/components/SystemStatus";
import { LeaderboardTable } from "@/components/LeaderboardTable";

interface MarketCard {
  id: string;
  question: string;
  category: string;
  yes_price: number;
  change_24h: number;
  volume_24h: number;
}

export default function Home() {
  const [topMarkets, setTopMarkets] = useState<MarketCard[]>([
    {
      id: "xi-jinping-out-before-2027",
      question: "Xi Jinping out before 2027?",
      category: "Geopolitics",
      yes_price: 0.0145,
      change_24h: -0.10,
      volume_24h: 20773.13,
    },
    {
      id: "will-alexandria-ocasio-cortez-win-the-2028-democratic-presidential-nomination",
      question: "Will Alexandria Ocasio-Cortez win the 2028 Democratic presidential nomination?",
      category: "Politics",
      yes_price: 0.1445,
      change_24h: -1.10,
      volume_24h: 14902.67,
    },
    {
      id: "will-gavin-newsom-win-the-2028-democratic-presidential-nomination",
      question: "Will Gavin Newsom win the 2028 Democratic presidential nomination?",
      category: "Politics",
      yes_price: 0.2250,
      change_24h: 1.20,
      volume_24h: 5243.57,
    },
    {
      id: "trump-presidential-win-2024",
      question: "Will Donald Trump win the 2024 Presidential Election?",
      category: "Politics",
      yes_price: 0.61,
      change_24h: 4.25,
      volume_24h: 42850000,
    },
  ]);

  useEffect(() => {
    const fetchMarkets = async () => {
      try {
        const res = await fetch("http://localhost:8080/markets?sort=volume&limit=4");
        if (res.ok) {
          const data = await res.json();
          if (Array.isArray(data) && data.length > 0) {
            setTopMarkets(data);
          }
        }
      } catch {}
    };
    fetchMarkets();
  }, []);

  return (
    <div className="space-y-6">
      <SystemStatus />

      {/* Featured Prediction Markets Banner */}
      <div className="space-y-3 font-mono">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <span className="w-2 h-2 rounded-full bg-accent animate-pulse" />
            <h2 className="text-xs font-semibold text-zinc-300 uppercase tracking-wider">
              Active CLOB Prediction Markets & Candlesticks
            </h2>
          </div>
          <Link
            href="/markets"
            className="text-xs text-accent hover:text-white transition-colors"
          >
            View All Markets ({topMarkets.length}) →
          </Link>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-3">
          {topMarkets.slice(0, 4).map((m) => (
            <Link
              key={m.id}
              href={`/market/${m.id}`}
              className="bg-card hover:bg-zinc-800/40 border border-border hover:border-zinc-700 p-3.5 rounded-lg transition-all group flex flex-col justify-between"
            >
              <div>
                <div className="flex items-center justify-between text-[10px] text-zinc-500 mb-1.5">
                  <span className="px-1.5 py-0.5 rounded bg-zinc-900 border border-zinc-800 text-zinc-400">
                    {m.category}
                  </span>
                  <span
                    className={`font-bold ${
                      m.change_24h >= 0 ? "text-success" : "text-danger"
                    }`}
                  >
                    {m.change_24h >= 0 ? "+" : ""}
                    {m.change_24h.toFixed(1)}%
                  </span>
                </div>
                <h3 className="text-xs font-medium text-white group-hover:text-accent transition-colors line-clamp-2">
                  {m.question}
                </h3>
              </div>

              <div className="mt-3 pt-2 border-t border-border/40 flex items-end justify-between">
                <div>
                  <span className="text-[10px] text-zinc-500 block">YES / ODDS</span>
                  <span className="text-sm font-bold text-emerald-400">
                    ${m.yes_price.toFixed(2)}
                  </span>
                  <span className="text-[10px] text-zinc-500 ml-1">
                    ({(m.yes_price * 100).toFixed(0)}%)
                  </span>
                </div>
                <span className="text-[10px] font-semibold text-zinc-400 group-hover:text-accent">
                  Chart ↗
                </span>
              </div>
            </Link>
          ))}
        </div>
      </div>

      <LeaderboardTable />
    </div>
  );
}
