import asyncio
import signal
import sys
from .config import IngestConfig
from .metrics import start_metrics_server
from .pipeline import IngestionPipeline

def main() -> None:
    print("========================================================")
    print("      poly-ingest: Real-time Polymarket Ingestion     ")
    print("========================================================")

    config = IngestConfig()

    # Start Prometheus metrics exporter
    try:
        start_metrics_server(config.metrics_port)
        print(f"[Main] Prometheus metrics server started on port {config.metrics_port}")
    except Exception as e:
        print(f"[Main] Warning: Could not start metrics server: {e}")

    pipeline = IngestionPipeline(config)
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    def shutdown():
        print("\n[Main] Shutdown signal received, stopping ingestion pipeline...")
        loop.create_task(pipeline.stop())

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, shutdown)
        except NotImplementedError:
            # Signal handlers not implemented on Windows event loop for add_signal_handler
            pass

    try:
        loop.run_until_complete(pipeline.start())
    except KeyboardInterrupt:
        loop.run_until_complete(pipeline.stop())
    finally:
        loop.close()
        print("[Main] Ingestor stopped cleanly.")

if __name__ == "__main__":
    main()
