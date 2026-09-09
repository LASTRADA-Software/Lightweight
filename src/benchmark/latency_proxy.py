#!/usr/bin/env python3
"""A TCP proxy that adds a fixed one-way delay to every chunk in both directions.

Puts a database server behind a simulated WAN link so a benchmark can be run against it at a
realistic round-trip time, without needing `tc netem` (which needs root). The round-trip time added
is 2 * --delay-ms.

    # PostgreSQL on :5432 reachable at :15432 with a 50 ms round-trip
    python3 src/benchmark/latency_proxy.py --listen 15432 --target 127.0.0.1:5432 --delay-ms 25
    ./LightweightPreparedStatementCacheBenchmark 10 "...Port=15432..." 3 single 20

Two caveats when reading results taken through it:

- The delay is applied per transmitted chunk, not per round-trip, so a request/response exchange
  costs about 2 * --delay-ms. Divide a measured per-query time by the round-trip time to recover
  the number of round-trips the query costs; that number is what carries over to a real link.
- `asyncio.sleep` cannot resolve much below a millisecond, so a --delay-ms under ~1 delivers more
  delay than asked for. Take the shape of the curve from the larger delays.
"""

import argparse
import asyncio


async def pipe(reader, writer, delay):
    try:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            if delay:
                await asyncio.sleep(delay)
            writer.write(data)
            await writer.drain()
    except (ConnectionResetError, BrokenPipeError, asyncio.IncompleteReadError):
        pass
    finally:
        try:
            writer.close()
        except Exception:
            pass


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", type=int, required=True)
    ap.add_argument("--target", required=True, help="host:port")
    ap.add_argument("--delay-ms", type=float, default=0.0, help="one-way delay; RTT is twice this")
    args = ap.parse_args()

    host, port = args.target.rsplit(":", 1)
    delay = args.delay_ms / 1000.0

    async def handle(client_reader, client_writer):
        try:
            server_reader, server_writer = await asyncio.open_connection(host, int(port))
        except OSError:
            client_writer.close()
            return
        await asyncio.gather(
            pipe(client_reader, server_writer, delay),
            pipe(server_reader, client_writer, delay),
        )

    server = await asyncio.start_server(handle, "127.0.0.1", args.listen)
    print(f"listening on 127.0.0.1:{args.listen} -> {args.target}, one-way {args.delay_ms} ms", flush=True)
    async with server:
        await server.serve_forever()


asyncio.run(main())
