"""serve.py on an ephemeral port: the COOP/COEP headers, Cache-Control no-cache, the MIME types,
the unchanged bytes of a part (no Content-Encoding), and none of the isolation headers with
--no-isolation-headers.

usage: test_serve.py <serve.py>
"""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import threading
import urllib.error
import urllib.request

spec = importlib.util.spec_from_file_location("serve", Path(sys.argv[1]).resolve())
serve = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serve)
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


FILES = {
    "index.html": ("text/html; charset=utf-8", b"<!doctype html>"),
    "vfs.js": ("text/javascript; charset=utf-8", b"export default 1;"),
    "vfs.mjs": ("text/javascript; charset=utf-8", b"export {};"),
    "vfs.wasm": ("application/wasm", b"\0asm\1\0\0\0"),
    "manifest.json": ("application/json", b"{}"),
    "media/c1.webm": ("video/webm", b"\x1a\x45\xdf\xa3v"),
    "media/c1.audio.webm": ("audio/webm", b"\x1a\x45\xdf\xa3a"),
    "cd/track02.opus": ("audio/ogg", b"OggS"),
    "data/" + "ab" * 32 + ".zst": ("application/zstd", b"\x28\xb5\x2f\xfd\x20\x00\x01\x00\x00"),
}


def fetch(port, name):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/{name}", timeout=10) as response:
        return response.status, response.headers, response.read()


def run(site, isolation):
    server = serve.make_server(site, 0, isolation=isolation, quiet=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        port = server.server_port
        for name, (mime, body) in FILES.items():
            status, headers, data = fetch(port, name)
            check(status == 200, f"{name}: {status}")
            check(headers.get("Content-Type") == mime, f"{name}: {headers.get('Content-Type')}")
            check(data == body and headers.get("Content-Encoding") is None, f"{name}: bytes changed")
            check(headers.get("Cache-Control") == "no-cache", f"{name}: Cache-Control {headers.get('Cache-Control')}")
            for header, value in serve.ISOLATION_HEADERS.items():
                got = headers.get(header)
                check(got == (value if isolation else None), f"{name}: {header} {got}")
        try:
            fetch(port, "missing.js")
            check(False, "a missing file was served")
        except urllib.error.HTTPError as e:
            check(e.code == 404, f"missing: {e.code}")
            check((e.headers.get("Cross-Origin-Embedder-Policy") is not None) == isolation, "headers on a 404")
    finally:
        server.shutdown()
        server.server_close()


with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as tmp:
    for name, (_, body) in FILES.items():
        path = Path(tmp) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(body)
    run(tmp, True)
    run(tmp, False)
print(f"{checks} checks passed")
