"""Synthetic protocol/publication tests; no original data or executable."""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

import es2shellprep as prep


class Tests(unittest.TestCase):
    def setUp(self):
        self.outputs = {name: (b"synthetic " + name.encode()) for name in prep.NAMES}
        self.outputs["mission.var"] = bytes(2000)

    def packet(self):
        parts = [b'{"synthetic":true}', *self.outputs.values()]
        return b"ES2P" + b"".join(struct.pack("<I", len(x)) + x for x in parts)

    def test_packet_and_all_truncations(self):
        packet = self.packet()
        self.assertEqual(prep.decode_packet(packet)[1], self.outputs)
        for n in range(len(packet)):
            with self.assertRaises((ValueError, UnicodeError)):
                prep.decode_packet(packet[:n])
        with self.assertRaises(ValueError):
            prep.decode_packet(packet + b"x")
        with self.assertRaises(ValueError):
            prep.decode_packet(b"ES2P" + struct.pack("<I", 0xffffffff))

    def test_emitted_save_block(self):
        parts = [b'{"emitted_save":1}', *self.outputs.values(), b"synthetic save"]
        packet = b"ES2P" + b"".join(struct.pack("<I", len(x)) + x for x in parts)
        metadata, outputs = prep.decode_packet(packet)
        self.assertEqual(outputs.pop(prep.SAVE_OUTPUT), b"synthetic save")
        self.assertEqual(outputs, self.outputs)
        with self.assertRaises(ValueError):
            prep.decode_packet(packet[:-1])
        with self.assertRaises(ValueError):
            prep.decode_packet(self.packet().replace(b'{"synthetic":true}', b'{"emitted_save":1}'))

    def test_forbidden_directories(self):
        repo = Path(__file__).resolve().parents[3]
        checkout = Path(tempfile.mkdtemp(dir=os.environ.get("TEST_TMPDIR")))
        (checkout / ".git").mkdir()
        (checkout / "build/installs/timing-r111").mkdir(parents=True)
        for path in (repo / "build/test", repo / "build/../test",
                     checkout / "build/installs/timing-r111/new-launch",
                     checkout / "build/installs"):
            with self.assertRaises(ValueError):
                prep.checked_destination(path)

    def test_manifest_with_synthetic_bytes(self):
        # Only the low-level writer is tested inside scratch. The public CLI
        # cannot bypass checked_destination, including for synthetic data.
        scratch = Path(os.environ.get("TEST_TMPDIR", Path(__file__).resolve().parents[3] /
                                      "build/codex/scratch-shell-invitro"))
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temp:
            path = Path(temp)
            prep.write_staging(path, "synthetic", self.outputs, dict(kind="synthetic"))
            manifest = json.loads((path / "manifest.json").read_text())
            self.assertEqual(manifest["schema"], 1)
            self.assertEqual(manifest["files"], prep.file_entries(self.outputs))
            for name, data in self.outputs.items():
                self.assertEqual((path / name).read_bytes(), data)
            with self.assertRaises(FileExistsError):
                prep.write_staging(path, "synthetic", self.outputs, {})
            with self.assertRaises(ValueError):
                prep.checked_destination(path)

    def test_atomic_publication_and_immutability(self):
        scratch = os.environ.get("TEST_TMPDIR")
        if scratch is None:
            self.skipTest("publication test requires Bazel's external scratch")
        with tempfile.TemporaryDirectory(dir=scratch) as temp:
            destination = Path(temp) / "synthetic-launch"
            # Bazel's execroot contains a .git link. The real guard correctly
            # rejects it; exercise publication with synthetic bytes and a
            # narrowly scoped guard double. Real guard cases are separate.
            def synthetic_destination(path):
                path = Path(path).resolve()
                self.assertEqual(path, destination.resolve())
                if path.exists():
                    raise ValueError("exists")
                return path
            with patch.object(prep, "checked_destination", synthetic_destination):
                prep.publish_checked(destination, self.outputs, dict(kind="synthetic"))
                with self.assertRaises(ValueError):
                    prep.publish_checked(destination, self.outputs, {})
            self.assertEqual(sorted(x.name for x in destination.iterdir()),
                             sorted([*prep.NAMES, "manifest.json"]))
            for name, data in self.outputs.items():
                self.assertEqual((destination / name).read_bytes(), data)
            self.assertEqual(list(Path(temp).iterdir()), [destination])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
