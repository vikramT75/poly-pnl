import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "gravia-pnl | Real-Time Polymarket PnL Terminal",
  description: "High-performance institutional-grade PnL engine for Polymarket prediction markets",
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en" className="dark">
      <body className="bg-background text-zinc-100 font-mono antialiased min-h-screen selection:bg-accent selection:text-white">
        <header className="border-b border-border bg-card/60 backdrop-blur-md sticky top-0 z-50 px-6 py-3 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <span className="w-3 h-3 rounded-full bg-success animate-pulse" />
            <a href="/" className="font-bold text-lg tracking-wider text-white hover:text-accent transition-colors">
              GRAVIA <span className="text-zinc-500 font-normal">| POLYMARKET PnL ENGINE</span>
            </a>
          </div>
          <div className="flex items-center gap-6 text-xs text-zinc-400">
            <span className="bg-zinc-800/80 px-2.5 py-1 rounded border border-zinc-700/60 text-zinc-300">
              Chain: Polygon (137)
            </span>
            <span className="bg-zinc-800/80 px-2.5 py-1 rounded border border-zinc-700/60 text-zinc-300">
              Engine: C++20 Core
            </span>
          </div>
        </header>
        <main className="max-w-7xl mx-auto px-6 py-6">
          {children}
        </main>
      </body>
    </html>
  );
}
