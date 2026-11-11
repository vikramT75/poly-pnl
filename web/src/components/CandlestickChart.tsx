"use client";

import React, { useEffect, useRef, useState } from "react";
import {
  createChart,
  ColorType,
  IChartApi,
  ISeriesApi,
  CrosshairMode,
  UTCTimestamp,
} from "lightweight-charts";

export interface CandleData {
  time: number; // Unix timestamp in seconds
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
}

interface CandlestickChartProps {
  data: CandleData[];
  symbol?: string;
  selectedResolution: "1m" | "5m" | "15m" | "1h" | "1d";
  onResolutionChange: (res: "1m" | "5m" | "15m" | "1h" | "1d") => void;
  outcome: "YES" | "NO";
  onOutcomeChange: (outcome: "YES" | "NO") => void;
}

export function CandlestickChart({
  data,
  symbol = "POLYGON:POLYMARKET",
  selectedResolution,
  onResolutionChange,
  outcome,
  onOutcomeChange,
}: CandlestickChartProps) {
  const chartContainerRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<IChartApi | null>(null);
  const candleSeriesRef = useRef<ISeriesApi<"Candlestick"> | null>(null);
  const volumeSeriesRef = useRef<ISeriesApi<"Histogram"> | null>(null);

  const [hoveredData, setHoveredData] = useState<{
    time?: string;
    open?: number;
    high?: number;
    low?: number;
    close?: number;
    volume?: number;
    change?: number;
  } | null>(null);

  const [cursorPos, setCursorPos] = useState<{ x: number; y: number } | null>(null);

  // Latest candle for fallback stats when not hovered
  const latestCandle = data.length > 0 ? data[data.length - 1] : null;

  useEffect(() => {
    if (!chartContainerRef.current) return;

    // Create TradingView Chart
    const chart = createChart(chartContainerRef.current, {
      layout: {
        background: { type: ColorType.Solid, color: "#111116" },
        textColor: "#71717a",
        fontFamily: "JetBrains Mono, monospace, sans-serif",
        fontSize: 11,
      },
      grid: {
        vertLines: { color: "#1f1f28" },
        horzLines: { color: "#1f1f28" },
      },
      crosshair: {
        mode: CrosshairMode.Normal,
        vertLine: {
          color: "#3b82f6",
          width: 1,
          style: 3,
          labelBackgroundColor: "#1f1f28",
        },
        horzLine: {
          color: "#3b82f6",
          width: 1,
          style: 3,
          labelBackgroundColor: "#1f1f28",
        },
      },
      rightPriceScale: {
        borderColor: "#1f1f28",
        autoScale: true,
        scaleMargins: {
          top: 0.1,
          bottom: 0.25, // Leave bottom 25% for volume bars
        },
      },
      timeScale: {
        borderColor: "#1f1f28",
        timeVisible: true,
        secondsVisible: false,
      },
      handleScroll: {
        mouseWheel: true,
        pressedMouseMove: true,
      },
      handleScale: {
        axisPressedMouseMove: true,
        mouseWheel: true,
        pinch: true,
      },
    });

    chartRef.current = chart;

    // 1. Candlestick Series
    const candleSeries = chart.addCandlestickSeries({
      upColor: "#10b981",
      downColor: "#ef4444",
      borderUpColor: "#10b981",
      borderDownColor: "#ef4444",
      wickUpColor: "#10b981",
      wickDownColor: "#ef4444",
      priceFormat: {
        type: "custom",
        formatter: (price: number) => `$${price.toFixed(3)}`,
      },
    });
    candleSeriesRef.current = candleSeries;

    // 2. Volume Histogram Series (pinned to bottom)
    const volumeSeries = chart.addHistogramSeries({
      priceFormat: {
        type: "volume",
      },
      priceScaleId: "", // Overlay on separate virtual scale
    });
    volumeSeries.priceScale().applyOptions({
      scaleMargins: {
        top: 0.8, // Start at 80% down
        bottom: 0,
      },
    });
    volumeSeriesRef.current = volumeSeries;

    // Crosshair listener for real-time tooltip stats
    chart.subscribeCrosshairMove((param) => {
      if (!param || !param.point || !param.time || !param.seriesData) {
        setHoveredData(null);
        setCursorPos(null);
        return;
      }

      const candle = param.seriesData.get(candleSeries) as {
        open?: number;
        high?: number;
        low?: number;
        close?: number;
      } | undefined;

      const vol = param.seriesData.get(volumeSeries) as {
        value?: number;
      } | undefined;

      if (candle && candle.open !== undefined && candle.close !== undefined) {
        const date = new Date((param.time as number) * 1000);
        const timeStr = date.toUTCString().replace("GMT", "UTC");
        const change = ((candle.close - candle.open) / candle.open) * 100;

        setHoveredData({
          time: timeStr,
          open: candle.open,
          high: candle.high,
          low: candle.low,
          close: candle.close,
          volume: vol?.value,
          change: change,
        });
        setCursorPos({ x: param.point.x, y: param.point.y });
      } else {
        setHoveredData(null);
        setCursorPos(null);
      }
    });

    const containerEl = chartContainerRef.current;
    const handleMouseLeave = () => {
      setHoveredData(null);
      setCursorPos(null);
    };
    containerEl?.addEventListener("mouseleave", handleMouseLeave);

    // Handle auto-resizing
    const resizeObserver = new ResizeObserver((entries) => {
      if (entries.length === 0 || !entries[0].contentRect) return;
      const { width, height } = entries[0].contentRect;
      chart.applyOptions({ width, height });
    });
    resizeObserver.observe(chartContainerRef.current);

    return () => {
      containerEl?.removeEventListener("mouseleave", handleMouseLeave);
      resizeObserver.disconnect();
      chart.remove();
      chartRef.current = null;
    };
  }, []);

  // Sync data updates to chart series
  useEffect(() => {
    if (!candleSeriesRef.current || !volumeSeriesRef.current || data.length === 0) return;

    const formattedCandles = data.map((d) => ({
      time: d.time as UTCTimestamp,
      open: d.open,
      high: d.high,
      low: d.low,
      close: d.close,
    }));

    const formattedVolume = data.map((d) => ({
      time: d.time as UTCTimestamp,
      value: d.volume,
      color: d.close >= d.open ? "rgba(16, 185, 129, 0.4)" : "rgba(239, 68, 68, 0.4)",
    }));

    candleSeriesRef.current.setData(formattedCandles);
    volumeSeriesRef.current.setData(formattedVolume);

    if (chartRef.current) {
      chartRef.current.timeScale().fitContent();
    }
  }, [data]);

  const activeStats = hoveredData || (latestCandle ? {
    open: latestCandle.open,
    high: latestCandle.high,
    low: latestCandle.low,
    close: latestCandle.close,
    volume: latestCandle.volume,
    change: ((latestCandle.close - latestCandle.open) / latestCandle.open) * 100,
  } : null);

  return (
    <div className="bg-card rounded-lg border border-border overflow-hidden flex flex-col">
      {/* Top Bar: Controls, Timeframe Selector & Contract Outcome Toggle */}
      <div className="p-3 border-b border-border flex flex-wrap items-center justify-between gap-3 bg-zinc-950/40">
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5 font-bold text-xs tracking-wider text-white">
            <span className="w-2 h-2 rounded-full bg-accent animate-pulse" />
            <span>{symbol}</span>
          </div>

          {/* Outcome Contract Switcher (YES / NO) */}
          <div className="flex items-center bg-zinc-900 border border-border rounded p-0.5 text-xs font-semibold">
            <button
              onClick={() => onOutcomeChange("YES")}
              className={`px-3 py-1 rounded transition-colors ${
                outcome === "YES"
                  ? "bg-emerald-600 text-white shadow-sm"
                  : "text-zinc-400 hover:text-white"
              }`}
            >
              YES Contract
            </button>
            <button
              onClick={() => onOutcomeChange("NO")}
              className={`px-3 py-1 rounded transition-colors ${
                outcome === "NO"
                  ? "bg-rose-600 text-white shadow-sm"
                  : "text-zinc-400 hover:text-white"
              }`}
            >
              NO Contract
            </button>
          </div>
        </div>

        {/* Resolution Selector Toolbar */}
        <div className="flex items-center gap-1 bg-zinc-900/80 border border-border rounded p-0.5 text-xs">
          {(["1m", "5m", "15m", "1h", "1d"] as const).map((res) => (
            <button
              key={res}
              onClick={() => onResolutionChange(res)}
              className={`px-2.5 py-1 rounded uppercase font-mono font-medium transition-colors ${
                selectedResolution === res
                  ? "bg-accent text-white"
                  : "text-zinc-400 hover:text-zinc-200 hover:bg-zinc-800"
              }`}
            >
              {res}
            </button>
          ))}
          <button
            onClick={() => chartRef.current?.timeScale().fitContent()}
            className="px-2 py-1 text-zinc-500 hover:text-zinc-300 ml-1 border-l border-zinc-800"
            title="Reset Zoom / Fit Content"
          >
            Fit
          </button>
        </div>
      </div>

      {/* Floating OHLCV Crosshair Legend */}
      <div className="px-4 py-2 bg-zinc-900/30 border-b border-border/60 flex flex-wrap items-center gap-4 text-xs font-mono">
        {activeStats ? (
          <>
            <div>
              <span className="text-zinc-500 mr-1">O:</span>
              <span className="text-zinc-200 font-semibold">${activeStats.open?.toFixed(3)}</span>
            </div>
            <div>
              <span className="text-zinc-500 mr-1">H:</span>
              <span className="text-emerald-400 font-semibold">${activeStats.high?.toFixed(3)}</span>
            </div>
            <div>
              <span className="text-zinc-500 mr-1">L:</span>
              <span className="text-rose-400 font-semibold">${activeStats.low?.toFixed(3)}</span>
            </div>
            <div>
              <span className="text-zinc-500 mr-1">C:</span>
              <span className="text-zinc-200 font-semibold">${activeStats.close?.toFixed(3)}</span>
            </div>
            <div>
              <span className="text-zinc-500 mr-1">CHG:</span>
              <span
                className={`font-semibold ${
                  (activeStats.change ?? 0) >= 0 ? "text-success" : "text-danger"
                }`}
              >
                {(activeStats.change ?? 0) >= 0 ? "+" : ""}
                {activeStats.change?.toFixed(2)}%
              </span>
            </div>
            <div>
              <span className="text-zinc-500 mr-1">VOL:</span>
              <span className="text-zinc-300 font-semibold">
                ${activeStats.volume ? (activeStats.volume / 1000).toFixed(1) + "k" : "0k"}
              </span>
            </div>
            {hoveredData?.time && (
              <div className="ml-auto text-zinc-500 hidden md:block">
                {hoveredData.time}
              </div>
            )}
          </>
        ) : (
          <span className="text-zinc-500">Hover over chart candles for tick details</span>
        )}
      </div>

      {/* Chart Canvas Container */}
      <div className="relative w-full h-[440px]" ref={chartContainerRef}>
        {cursorPos && hoveredData && hoveredData.close !== undefined && (
          <div
            className={`absolute pointer-events-none z-20 flex flex-col items-center bg-zinc-950/95 border border-zinc-700/80 px-2.5 py-1.5 rounded-md shadow-xl text-[11px] font-mono transform -translate-x-1/2 backdrop-blur-md ${
              cursorPos.y < 55 ? "translate-y-3" : "-translate-y-full -mt-2.5"
            } transition-all duration-75`}
            style={{
              left: `${Math.max(75, Math.min(cursorPos.x, (chartContainerRef.current?.clientWidth ?? 400) - 75))}px`,
              top: `${cursorPos.y}px`,
            }}
          >
            <div className="flex items-center gap-1.5 whitespace-nowrap">
              <span className="font-bold text-white">${hoveredData.close.toFixed(3)}</span>
              <span className="font-semibold text-emerald-400">
                ({(hoveredData.close * 100).toFixed(1)}%)
              </span>
              <span
                className={`text-[10px] ${
                  (hoveredData.change ?? 0) >= 0 ? "text-success" : "text-danger"
                }`}
              >
                {(hoveredData.change ?? 0) >= 0 ? "+" : ""}
                {(hoveredData.change ?? 0).toFixed(1)}%
              </span>
            </div>
            {cursorPos.y >= 55 ? (
              <div className="w-0 h-0 border-l-[4px] border-l-transparent border-r-[4px] border-r-transparent border-t-[4px] border-t-zinc-700 absolute -bottom-1 left-1/2 transform -translate-x-1/2" />
            ) : (
              <div className="w-0 h-0 border-l-[4px] border-l-transparent border-r-[4px] border-r-transparent border-b-[4px] border-b-zinc-700 absolute -top-1 left-1/2 transform -translate-x-1/2" />
            )}
          </div>
        )}
      </div>
    </div>
  );
}
