#!/usr/bin/env python3
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import open_citadel_import as oc


def make_obb(path: Path, files):
    names = []
    table_size = len(oc.OBB_MAGIC) + len(oc.OBB_PREAMBLE)
    for name, payload in files:
        raw = name.encode("utf-8") + b"\0"
        names.append((raw, payload))
        table_size += 4 + len(raw) + 8 + 4

    offset = table_size
    with path.open("wb") as f:
        f.write(oc.OBB_MAGIC)
        f.write(oc.OBB_PREAMBLE)
        for raw, payload in names:
            f.write(struct.pack("<I", len(raw)))
            f.write(raw)
            f.write(struct.pack("<QI", offset, len(payload)))
            offset += len(payload)
        for _, payload in names:
            f.write(payload)


class OpenCitadelObbTests(unittest.TestCase):
    def test_index_and_extract(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            obb = root / "test.obb"
            files = [
                ("..\\Engine\\Shaders\\Mobile\\DefaultPixelShader.msf", b"pixel"),
                ("..\\UDKGame\\CookedAndroid\\EpicCitadel.xxx", b"citadel"),
                ("..\\UDKGame\\AndroidTOC.txt", b"toc"),
            ]
            make_obb(obb, files)
            entries = oc.read_ue3_obb_index(obb)
            self.assertEqual(len(entries), 3)
            self.assertEqual(entries[0][1], entries[-1][1] - len(b"pixel") - len(b"citadel"))

            out = root / "out"
            written = oc.extract_ue3_obb(obb, out)
            self.assertEqual(len(written), 3)
            self.assertEqual(
                (out / "UDKGame" / "CookedAndroid" / "EpicCitadel.xxx").read_bytes(),
                b"citadel",
            )
            self.assertEqual((out / "UDKGame" / "AndroidTOC.txt").read_bytes(), b"toc")

    def test_rejects_trailing_data(self):
        with tempfile.TemporaryDirectory() as td:
            obb = Path(td) / "test.obb"
            make_obb(obb, [("..\\UDKGame\\AndroidTOC.txt", b"toc")])
            with obb.open("ab") as f:
                f.write(b"junk")
            with self.assertRaises(oc.DonorError):
                oc.read_ue3_obb_index(obb)

    def test_rejects_noncontiguous_payload(self):
        with tempfile.TemporaryDirectory() as td:
            obb = Path(td) / "test.obb"
            make_obb(
                obb,
                [
                    ("..\\Engine\\a", b"a"),
                    ("..\\Engine\\b", b"b"),
                ],
            )
            data = bytearray(obb.read_bytes())
            # Locate the second record by parsing the first one and increment its
            # recorded offset without moving its actual payload.
            pos = len(oc.OBB_MAGIC) + len(oc.OBB_PREAMBLE)
            n = struct.unpack_from("<I", data, pos)[0]
            pos += 4 + n + 12
            n = struct.unpack_from("<I", data, pos)[0]
            pos += 4 + n
            off = struct.unpack_from("<Q", data, pos)[0]
            struct.pack_into("<Q", data, pos, off + 1)
            obb.write_bytes(data)
            with self.assertRaises(oc.DonorError):
                oc.read_ue3_obb_index(obb)

    def test_path_normalization(self):
        self.assertEqual(
            oc.safe_relpath("..\\UDKGame\\CookedAndroid\\Core.xxx"),
            Path("UDKGame/CookedAndroid/Core.xxx"),
        )
        for bad in ("/absolute", "../..", "../../../../../.."):
            with self.assertRaises(oc.DonorError):
                oc.safe_relpath(bad)


if __name__ == "__main__":
    unittest.main()
