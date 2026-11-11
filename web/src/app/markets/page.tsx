"use client";

import React, { useState, useEffect } from "react";
import Link from "next/link";

interface MarketSummary {
  id: string;
  condition_id: string;
  question: string;
  category: string;
  yes_price: number;
  no_price: number;
  change_24h: number;
  volume_24h: number;
  open_interest: number;
  status: string;
  resolution_date: string;
}

export default function MarketsPage() {
  const [markets, setMarkets] = useState<MarketSummary[]>([]);
  const [loading, setLoading] = useState<boolean>(true);
  const [searchQuery, setSearchQuery] = useState<string>("");
  const [debouncedQuery, setDebouncedQuery] = useState<string>("");
  const [selectedSort, setSelectedSort] = useState<"volume" | "probability" | "change" | "liquidity">("volume");
  const [selectedCategory, setSelectedCategory] = useState<string>("All");

  // Debounce search query input (300ms)
  useEffect(() => {
    const handler = setTimeout(() => {
      setDebouncedQuery(searchQuery);
    }, 300);
    return () => clearTimeout(handler);
  }, [searchQuery]);

  // Fetch either search results or top questions of the day
  useEffect(() => {
    let isCancelled = false;
    setLoading(true);

    const fetchMarkets = async () => {
      try {
        let url = "";
        if (debouncedQuery.trim()) {
          url = `http://localhost:8080/markets/search?q=${encodeURIComponent(debouncedQuery.trim())}&limit=50`;
        } else {
          url = `http://localhost:8080/markets?sort=${selectedSort}&category=${encodeURIComponent(selectedCategory)}&limit=50`;
        }

        const res = await fetch(url);
        if (res.ok && !isCancelled) {
          const data = await res.json();
          if (Array.isArray(data)) {
            setMarkets(data);
          }
        }
      } catch (err) {
        // Keep existing if fetch fails
      } finally {
        if (!isCancelled) setLoading(false);
      }
    };

    fetchMarkets();

    return () => {
      isCancelled = true;
    };
  }, [debouncedQuery, selectedSort, selectedCategory]);

  const categories = ["All", "Politics", "Crypto", "Geopolitics", "Macro", "Sports"];

  return (
    <div className="space-y-6 font-mono">
      {/* Title & Stats */}
      <div className="flex flex-wrap items-center justify-between gap-4">
        <div>
          <div className="flex items-center gap-2">
            <span className="w-2.5 h-2.5 rounded-full bg-emerald-500 animate-pulse" />
            <h1 className="text-xl font-bold text-white tracking-wide">
              LIVE PREDICTION MARKETS DISCOVERY
            </h1>
          </div>
          <p className="text-xs text-zinc-500 mt-1">
            Real-time Polymarket order books, implied probabilities, and 24h trading volume
          </p>
        </div>
      </div>

      {/* Search Input Bar (Option B: Clean Embedded Search Input) */}
      <div className="bg-card p-4 rounded-lg border border-border space-y-4">
        <div className="relative">
          <div className="absolute inset-y-0 left-0 pl-3.5 flex items-center pointer-events-none text-zinc-500">
            <svg
              className="w-4 h-4"
              fill="none"
              stroke="currentColor"
              viewBox="0 0 24 24"
            >
              <path
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeWidth={2}
                d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z"
              />
            </svg>
          </div>
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder="Search live Polymarket questions (e.g. Bitcoin, Fed, Trump, China, Election, AI, Interest Rate)..."
            className="w-full pl-10 pr-10 py-2.5 bg-zinc-900/90 border border-border focus:border-accent rounded-lg text-sm text-zinc-100 placeholder-zinc-500 focus:outline-none focus:ring-1 focus:ring-accent transition-all"
          />
          {searchQuery && (
            <button
              onClick={() => setSearchQuery("")}
              className="absolute inset-y-0 right-0 pr-3.5 flex items-center text-zinc-500 hover:text-zinc-200 text-xs"
            >
              ✕ Clear
            </button>
          )}
        </div>

        {/* Filters and Sorting Controls */}
        <div className="flex flex-wrap items-center justify-between gap-4 pt-1">
          {/* Category Badges */}
          <div className="flex flex-wrap gap-1.5 bg-zinc-900/80 p-1 rounded-lg border border-border text-xs">
            {categories.map((cat) => (
              <button
                key={cat}
                onClick={() => setSelectedCategory(cat)}
                className={`px-3 py-1 rounded transition-colors ${
                  selectedCategory === cat
                    ? "bg-accent text-white font-semibold"
                    : "text-zinc-400 hover:text-white"
                }`}
              >
                {cat}
              </button>
            ))}
          </div>

          {/* Sorting Pills */}
          <div className="flex items-center gap-1.5 text-xs">
            <span className="text-zinc-500 text-[11px] mr-1 hidden sm:inline">RANK BY:</span>
            {[
              { id: "volume", label: "Top 24h Volume" },
              { id: "probability", label: "Highest Probability" },
              { id: "change", label: "24h Volatility" },
              { id: "liquidity", label: "Deepest Liquidity" },
            ].map((s) => (
              <button
                key={s.id}
                onClick={() => setSelectedSort(s.id as any)}
                className={`px-2.5 py-1 rounded border transition-colors ${
                  selectedSort === s.id
                    ? "bg-zinc-800 border-zinc-600 text-white font-medium"
                    : "border-border text-zinc-400 hover:text-zinc-200 hover:bg-zinc-900"
                }`}
              >
                {s.label}
              </button>
            ))}
          </div>
        </div>
      </div>

      {/* Results Status Header */}
      <div className="flex items-center justify-between text-xs text-zinc-400 px-1">
        <div>
          {debouncedQuery ? (
            <span>
              Search results for <span className="text-accent font-semibold">"{debouncedQuery}"</span>:{" "}
              <strong className="text-white">{markets.length}</strong> matching questions found
            </span>
          ) : (
            <span>
              Top Questions of the Day: <strong className="text-white">{markets.length}</strong> contracts active
            </span>
          )}
        </div>
        {loading && (
          <div className="flex items-center gap-1.5 text-accent text-xs">
            <span className="w-2 h-2 rounded-full bg-accent animate-ping" />
            <span>Fetching live Polymarket order books...</span>
          </div>
        )}
      </div>

      {/* Markets Table */}
      <div className="bg-card rounded-lg border border-border overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs">
            <thead className="bg-zinc-900/60 text-zinc-400 border-b border-border text-[11px] uppercase">
              <tr>
                <th className="py-3 px-4 font-semibold">Question / Market Contract</th>
                <th className="py-3 px-4 font-semibold">Category</th>
                <th className="py-3 px-4 font-semibold text-right">YES Price (Odds)</th>
                <th className="py-3 px-4 font-semibold text-right">NO Price</th>
                <th className="py-3 px-4 font-semibold text-right">24h Momentum</th>
                <th className="py-3 px-4 font-semibold text-right">24h Volume</th>
                <th className="py-3 px-4 font-semibold text-right">Open Interest</th>
                <th className="py-3 px-4 font-semibold text-center">Terminal</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-border/40">
              {markets.length === 0 && !loading ? (
                <tr>
                  <td colSpan={8} className="py-12 text-center text-zinc-500">
                    <p className="text-sm text-zinc-400 font-semibold mb-1">
                      No active markets found matching "{debouncedQuery}"
                    </p>
                    <p className="text-xs text-zinc-500">
                      Try searching broader keywords like "Bitcoin", "Election", "Trump", "Fed", or "China".
                    </p>
                  </td>
                </tr>
              ) : (
                markets.map((m) => (
                  <tr key={m.id} className="hover:bg-zinc-800/40 transition-colors">
                    <td className="py-3.5 px-4 font-medium text-white max-w-md">
                      <Link
                        href={`/market/${m.id}`}
                        className="hover:text-accent transition-colors block text-sm"
                      >
                        {m.question}
                      </Link>
                      <span className="text-[10px] text-zinc-500 font-mono block mt-0.5">
                        Cond: {m.condition_id.slice(0, 10)}...{m.condition_id.slice(-8)}
                      </span>
                    </td>
                    <td className="py-3.5 px-4">
                      <span className="px-2 py-0.5 rounded text-[10px] font-bold bg-zinc-800 border border-zinc-700 text-zinc-300">
                        {m.category}
                      </span>
                    </td>
                    <td className="py-3.5 px-4 text-right">
                      <span className="font-bold text-emerald-400 text-sm">
                        ${m.yes_price.toFixed(3)}
                      </span>
                      <span className="text-[10px] text-zinc-500 block">
                        {(m.yes_price * 100).toFixed(1)}% Implied
                      </span>
                    </td>
                    <td className="py-3.5 px-4 text-right">
                      <span className="font-bold text-rose-400 text-sm">
                        ${m.no_price.toFixed(3)}
                      </span>
                      <span className="text-[10px] text-zinc-500 block">
                        {(m.no_price * 100).toFixed(1)}% Implied
                      </span>
                    </td>
                    <td
                      className={`py-3.5 px-4 text-right font-bold ${
                        m.change_24h >= 0 ? "text-success" : "text-danger"
                      }`}
                    >
                      {m.change_24h >= 0 ? "+" : ""}
                      {m.change_24h.toFixed(2)}%
                    </td>
                    <td className="py-3.5 px-4 text-right text-zinc-200 font-semibold">
                      ${m.volume_24h >= 1000000
                        ? (m.volume_24h / 1000000).toFixed(2) + "M"
                        : (m.volume_24h / 1000).toFixed(1) + "K"}
                    </td>
                    <td className="py-3.5 px-4 text-right text-zinc-400">
                      ${m.open_interest >= 1000000
                        ? (m.open_interest / 1000000).toFixed(2) + "M"
                        : (m.open_interest / 1000).toFixed(1) + "K"}
                    </td>
                    <td className="py-3.5 px-4 text-center">
                      <Link
                        href={`/market/${m.id}`}
                        className="px-2.5 py-1 bg-accent/20 border border-accent/40 text-accent hover:bg-accent hover:text-white rounded text-[11px] font-semibold transition-colors inline-block whitespace-nowrap"
                      >
                        Chart ↗
                      </Link>
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
