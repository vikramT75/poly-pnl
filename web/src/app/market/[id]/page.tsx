"use client";

import React, { useState, useEffect } from "react";
import Link from "next/link";
import { CandlestickChart, CandleData } from "@/components/CandlestickChart";
import { OrderBookDepth, DepthLevel } from "@/components/OrderBookDepth";
import { TradeTape, TradeItem } from "@/components/TradeTape";

interface MarketData {
  market_id: string;
  question: string;
  category: string;
  status: string;
  yes_price: number;
  no_price: number;
  change_24h: number;
  volume_24h: number;
  open_interest: number;
  resolution_date: string;
  oracle: string;
  top_holders: { wallet: string; shares: number; side: string }[];
}

interface PageProps {
  params: {
    id: string;
  };
}

export default function MarketPage({ params }: PageProps) {
  const { id } = params;

  const [market, setMarket] = useState<MarketData>({
    market_id: id,
    question: "Will Donald Trump win the 2024 US Presidential Election?",
    category: "Politics",
    status: "Active",
    yes_price: 0.61,
    no_price: 0.39,
    change_24h: 4.25,
    volume_24h: 42850000,
    open_interest: 89400000,
    resolution_date: "2024-11-05",
    oracle: "UMA Optimistic Oracle v2",
    top_holders: [
      { wallet: "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E", shares: 85000, side: "YES" },
      { wallet: "0xC5d563A36AE78145C45a50134d48A1215220f80a", shares: 42000, side: "NO" },
      { wallet: "0x10455b94be059c2331dfc956ec945892576b978b", shares: 31000, side: "YES" },
    ],
  });

  const [resolution, setResolution] = useState<"1m" | "5m" | "15m" | "1h" | "1d">("15m");
  const [outcome, setOutcome] = useState<"YES" | "NO">("YES");
  const [candles, setCandles] = useState<CandleData[]>([]);
  const [depth, setDepth] = useState<{
    bids: DepthLevel[];
    asks: DepthLevel[];
    midpoint: number;
    spread: number;
    implied_probability: string;
  }>({
    bids: [],
    asks: [],
    midpoint: 0.61,
    spread: 0.02,
    implied_probability: "61.0%",
  });
  const [trades, setTrades] = useState<TradeItem[]>([]);

  // Fetch Market Details, Candles, Depth & Trades
  useEffect(() => {
    const fetchMarketInfo = async () => {
      try {
        const res = await fetch(`http://localhost:8080/market/${id}`);
        if (res.ok) {
          const data = await res.json();
          setMarket(data);
        }
      } catch {}
    };

    const fetchCandles = async () => {
      try {
        const res = await fetch(
          `http://localhost:8080/market/${id}/ohlcv?outcome=${outcome}&resolution=${resolution}&limit=120`
        );
        if (res.ok) {
          const data = await res.json();
          setCandles(data);
        }
      } catch {}
    };

    const fetchDepth = async () => {
      try {
        const res = await fetch(`http://localhost:8080/market/${id}/depth`);
        if (res.ok) {
          const data = await res.json();
          setDepth(data);
        }
      } catch {}
    };

    const fetchTrades = async () => {
      try {
        const res = await fetch(`http://localhost:8080/market/${id}/trades?limit=30`);
        if (res.ok) {
          const data = await res.json();
          setTrades(data);
        }
      } catch {}
    };

    fetchMarketInfo();
    fetchCandles();
    fetchDepth();
    fetchTrades();

    const interval = setInterval(() => {
      fetchCandles();
      fetchDepth();
      fetchTrades();
    }, 4000);

    return () => clearInterval(interval);
  }, [id, resolution, outcome]);

  return (
    <div className="space-y-6">
      {/* Navigation Breadcrumb */}
      <div className="flex items-center gap-2 text-xs text-zinc-500 font-mono">
        <Link href="/" className="hover:text-zinc-300">TERMINAL</Link>
        <span>/</span>
        <Link href="/markets" className="hover:text-zinc-300">MARKETS</Link>
        <span>/</span>
        <span className="text-zinc-300 uppercase">{id}</span>
      </div>

      {/* Market Headline Banner */}
      <div className="bg-card p-5 rounded-lg border border-border flex flex-wrap items-center justify-between gap-4">
        <div className="space-y-1.5 max-w-3xl">
          <div className="flex items-center gap-2">
            <span className="px-2 py-0.5 rounded text-[10px] font-bold bg-accent/20 border border-accent/40 text-accent uppercase">
              {market.category}
            </span>
            <span className="px-2 py-0.5 rounded text-[10px] font-bold bg-emerald-950/80 border border-emerald-800/40 text-emerald-400 uppercase">
              {market.status}
            </span>
            <span className="text-zinc-500 text-xs font-mono">
              Oracle: {market.oracle}
            </span>
          </div>
          <h1 className="text-lg md:text-xl font-bold text-white tracking-wide">
            {market.question}
          </h1>
        </div>

        {/* Quick Market Stats */}
        <div className="flex items-center gap-6 text-xs font-mono">
          <div className="flex flex-col">
            <span className="text-zinc-500 text-[10px]">24H VOLUME</span>
            <span className="text-sm font-bold text-zinc-200 mt-0.5">
              ${(market.volume_24h).toLocaleString()}
            </span>
          </div>

          <div className="flex flex-col">
            <span className="text-zinc-500 text-[10px]">OPEN INTEREST</span>
            <span className="text-sm font-bold text-zinc-300 mt-0.5">
              ${(market.open_interest).toLocaleString()}
            </span>
          </div>

          <div className="flex flex-col">
            <span className="text-zinc-500 text-[10px]">24H CHANGE</span>
            <span
              className={`text-sm font-bold mt-0.5 ${
                market.change_24h >= 0 ? "text-success" : "text-danger"
              }`}
            >
              {market.change_24h >= 0 ? "+" : ""}
              {market.change_24h.toFixed(2)}%
            </span>
          </div>
        </div>
      </div>

      {/* Main Trading Terminal Grid */}
      <div className="grid grid-cols-1 lg:grid-cols-4 gap-6">
        {/* Left 3 Columns: Candlestick Chart & Trade Tape */}
        <div className="lg:col-span-3 space-y-6">
          <CandlestickChart
            data={candles}
            symbol={`POLYMARKET:${id.toUpperCase()}`}
            selectedResolution={resolution}
            onResolutionChange={setResolution}
            outcome={outcome}
            onOutcomeChange={setOutcome}
          />

          <TradeTape trades={trades} />
        </div>

        {/* Right 1 Column: Orderbook Depth & Market Mechanics */}
        <div className="space-y-6">
          {/* Contract Price Cards */}
          <div className="grid grid-cols-2 gap-3">
            <div className="bg-emerald-950/30 border border-emerald-800/40 p-3 rounded-lg flex flex-col font-mono">
              <span className="text-[10px] text-emerald-400 font-bold uppercase tracking-wider">
                YES Price
              </span>
              <span className="text-2xl font-bold text-emerald-400 mt-1">
                ${market.yes_price.toFixed(2)}
              </span>
              <span className="text-[10px] text-zinc-500 mt-0.5">
                {(market.yes_price * 100).toFixed(0)}¢ Implied Odds
              </span>
            </div>

            <div className="bg-rose-950/30 border border-rose-800/40 p-3 rounded-lg flex flex-col font-mono">
              <span className="text-[10px] text-rose-400 font-bold uppercase tracking-wider">
                NO Price
              </span>
              <span className="text-2xl font-bold text-rose-400 mt-1">
                ${market.no_price.toFixed(2)}
              </span>
              <span className="text-[10px] text-zinc-500 mt-0.5">
                {(market.no_price * 100).toFixed(0)}¢ Implied Odds
              </span>
            </div>
          </div>

          {/* Orderbook Depth Bars */}
          <div className="h-[460px]">
            <OrderBookDepth
              bids={depth.bids}
              asks={depth.asks}
              midpoint={depth.midpoint}
              spread={depth.spread}
              impliedProbability={depth.implied_probability}
            />
          </div>

          {/* Top Token Holders in this Market */}
          <div className="bg-card rounded-lg border border-border p-4 font-mono text-xs">
            <span className="text-[10px] font-semibold text-zinc-400 uppercase tracking-wider block mb-3">
              Largest Position Holders
            </span>
            <div className="divide-y divide-border/40">
              {market.top_holders.map((holder, idx) => (
                <div key={idx} className="py-2 flex items-center justify-between">
                  <div className="flex items-center gap-2">
                    <span className="text-zinc-500">#{idx + 1}</span>
                    <Link
                      href={`/wallet/${holder.wallet}`}
                      className="text-zinc-300 hover:text-accent underline"
                    >
                      {holder.wallet.slice(0, 6)}...{holder.wallet.slice(-4)}
                    </Link>
                  </div>
                  <div className="flex items-center gap-2">
                    <span className="text-zinc-400">
                      {holder.shares.toLocaleString()}
                    </span>
                    <span
                      className={`px-1.5 py-0.5 rounded text-[10px] font-bold ${
                        holder.side === "YES"
                          ? "bg-emerald-950 text-emerald-400"
                          : "bg-rose-950 text-rose-400"
                      }`}
                    >
                      {holder.side}
                    </span>
                  </div>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
