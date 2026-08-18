#!/usr/bin/env python3
"""Export Eden DEM if needed, then serve the GBRS 3D coverage viewer."""

from __future__ import annotations

import sys
import webbrowser
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

WEB_DIR = Path(__file__).resolve().parent
if str(WEB_DIR) not in sys.path:
    sys.path.insert(0, str(WEB_DIR))
if str(WEB_DIR.parent) not in sys.path:
    sys.path.insert(0, str(WEB_DIR.parent))

from export_dem import DATA_DIR, export  # noqa: E402

PORT = 8765


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB_DIR), **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        sys.stdout.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))


def main() -> int:
    meta = DATA_DIR / "meta.json"
    terrain = DATA_DIR / "terrain.f32"
    if not meta.is_file() or not terrain.is_file():
        print("Exporting DEM for the viewer...", flush=True)
        export()
    else:
        print("Using existing %s" % meta, flush=True)

    url = "http://127.0.0.1:%d/" % PORT
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("GBRS 3D coverage viewer: %s" % url, flush=True)
    print("Ctrl+C to stop.", flush=True)
    webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
