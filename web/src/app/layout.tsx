import type { Metadata } from "next";
import Link from "next/link";
import "./globals.css";

export const metadata: Metadata = {
  title: "poly-pnl | Real-Time Polymarket PnL Terminal",
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
        <header className="border-b border-border bg-card/80 backdrop-blur-md sticky top-0 z-50 px-6 py-3 flex flex-wrap items-center justify-between gap-4">
          <div className="flex items-center gap-6">
            <div className="flex items-center gap-3">
              <Link href="/" className="font-bold text-lg tracking-wider text-white hover:text-accent transition-colors">
                POLYPNL <span className="text-zinc-500 font-normal">| PREDICTION TERMINAL</span>
              </Link>
            </div>

            {/* Navigation Links */}
            <nav className="flex items-center gap-1 bg-zinc-900 border border-border p-1 rounded-lg text-xs">
              <Link
                href="/"
                className="px-3 py-1 rounded text-zinc-300 hover:text-white hover:bg-zinc-800 transition-colors font-medium"
              >
                LEADERBOARD
              </Link>
              <Link
                href="/markets"
                className="px-3 py-1 rounded text-zinc-300 hover:text-white hover:bg-zinc-800 transition-colors font-medium"
              >
                MARKETS & CHARTS
              </Link>
              <a
                href="http://localhost:8080/docs"
                target="_blank"
                rel="noreferrer"
                className="px-3 py-1 rounded text-zinc-400 hover:text-accent hover:bg-zinc-800 transition-colors"
              >
                API DOCS ↗
              </a>
              <a
                href="http://localhost:3001"
                target="_blank"
                rel="noreferrer"
                className="px-3 py-1 rounded text-zinc-400 hover:text-accent hover:bg-zinc-800 transition-colors"
              >
                GRAFANA ↗
              </a>
            </nav>
          </div>

          <div className="flex items-center text-xs">
            <div className="flex items-center gap-2 bg-zinc-900 border border-zinc-800 px-3 py-1.5 rounded-md text-zinc-300">
              <span className="w-2 h-2 rounded-full bg-purple-500 shadow-[0_0_6px_rgba(168,85,247,0.5)]" />
              <span className="text-zinc-400 font-medium">Chain:</span>
              <span className="font-semibold text-zinc-200">Polygon (137)</span>
            </div>
          </div>
        </header>

        <main className="max-w-7xl mx-auto px-6 py-6">
          {children}
        </main>
      </body>
    </html>
  );
}
