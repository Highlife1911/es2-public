"""Serves the built site locally (docs/web_build.md section 17).

With the isolation headers (the default) every response carries
Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp, so the
page gets SharedArrayBuffer without its service worker. --no-isolation-headers serves the
site as a plain static host does, to test the service worker that sets them up.

usage: serve.py [--dir build/web/site] [--port 8000] [--bind 127.0.0.1] [--no-isolation-headers]
"""
import argparse
import functools
import http.server
from pathlib import Path
import sys

ISOLATION_HEADERS = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
}

# Parts are served as stored: no Content-Encoding, the worker decodes them itself.
MIME_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8",
    ".wasm": "application/wasm",
    ".json": "application/json",
    ".webm": "video/webm",
    ".opus": "audio/ogg",
    ".zst": "application/zstd",
}


class SiteHandler(http.server.SimpleHTTPRequestHandler):
    isolation = True

    def guess_type(self, path):
        name = str(path).lower()
        if name.endswith(".audio.webm"):
            return "audio/webm"
        return MIME_TYPES.get(Path(name).suffix, "application/octet-stream")

    def end_headers(self):
        # A rebuilt es2shell.wasm or es2sim.wasm must reach the game worker, whose fetches a
        # reload with Ctrl+F5 does not reliably bypass the HTTP cache for.
        self.send_header("Cache-Control", "no-cache")
        if self.isolation:
            for name, value in ISOLATION_HEADERS.items():
                self.send_header(name, value)
        super().end_headers()

    def log_message(self, format, *args):
        sys.stderr.write("%s %s\n" % (self.address_string(), format % args))


def make_server(directory, port=8000, bind="127.0.0.1", isolation=True, quiet=False):
    """A ThreadingHTTPServer over `directory`; port 0 picks a free port (server.server_port)."""
    attrs = {"isolation": isolation}
    if quiet:
        attrs["log_message"] = lambda self, format, *args: None
    handler = type("Handler", (SiteHandler,), attrs)
    return http.server.ThreadingHTTPServer((bind, port), functools.partial(handler, directory=str(directory)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dir", default="build/web/site")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--no-isolation-headers", action="store_true")
    args = ap.parse_args(argv)
    if not Path(args.dir).is_dir():
        print(f"serve: no site at {args.dir}", file=sys.stderr)
        return 1
    server = make_server(args.dir, args.port, args.bind, not args.no_isolation_headers)
    mode = "without isolation headers" if args.no_isolation_headers else "with COOP/COEP"
    print(f"serving {Path(args.dir).resolve()} at http://{args.bind}:{server.server_port}/ {mode}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
