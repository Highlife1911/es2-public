"""SHA-256 over 'sha256  relative/path' lines of a directory tree (sorted by path)."""
import hashlib
import sys
from pathlib import Path


def digest(root: Path) -> tuple[str, int]:
    files = sorted((p.relative_to(root).as_posix(), p) for p in root.rglob("*") if p.is_file() and "__pycache__" not in p.parts)
    lines = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {name}\n" for name, p in files)
    return hashlib.sha256(lines.encode()).hexdigest(), len(files)


if __name__ == "__main__":
    value, count = digest(Path(sys.argv[1]).resolve(strict=True))
    print(f"{value} {count}")
