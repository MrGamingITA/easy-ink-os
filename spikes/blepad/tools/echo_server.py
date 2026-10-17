#!/usr/bin/env python3
# SPIKE usa e getta · blepad: server di eco TCP per il ping della scheda (rimanda ogni riga com'è).
# Uso: python3 tools/echo_server.py [porta]   (predefinita 7777)
import asyncio
import sys


async def handle(reader, writer):
    peer = writer.get_extra_info("peername")
    print(f"collegata {peer}", flush=True)
    n = 0
    try:
        while line := await reader.readline():
            writer.write(line)
            await writer.drain()
            n += 1
    finally:
        print(f"scollegata {peer} dopo {n} righe", flush=True)
        writer.close()


async def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
    srv = await asyncio.start_server(handle, "0.0.0.0", port)
    print(f"eco in ascolto sulla porta {port}", flush=True)
    async with srv:
        await srv.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
