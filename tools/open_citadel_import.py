#!/usr/bin/env python3
"""Open Citadel donor importer.

Extract Epic Citadel 1.07 from the APKPure XAPK donor. No proprietary payload
is distributed by this tool; users must provide their own donor.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import tempfile
import zipfile

PACKAGE = "com.epicgames.EpicCitadel"
VERSION_CODE = 903107
VERSION_NAME = "1.07"
OBB_NAME = f"main.{VERSION_CODE}.{PACKAGE}.obb"
NATIVE_PATH = "lib/armeabi-v7a/libUnrealEngine3.so"
NATIVE_SHA256 = "39f30710ea08c8f89db3e0a8a907813acbc4c1f9173255a8f458324b3d0454fa"
OBB_MAGIC = b"UE3AndroidOBB|"
OBB_PREAMBLE = b"\x01\x00\x00"


class DonorError(RuntimeError):
    pass


def sha256_stream(fp) -> str:
    h = hashlib.sha256()
    for chunk in iter(lambda: fp.read(1024 * 1024), b""):
        h.update(chunk)
    return h.hexdigest()


def safe_relpath(ue3_name: str) -> Path:
    # Stored names are e.g. '..\\UDKGame\\CookedAndroid\\Core.xxx'.
    normalized = ue3_name.replace("\\", "/")
    while normalized.startswith("../"):
        normalized = normalized[3:]
    p = PurePosixPath(normalized)
    if p.is_absolute() or not p.parts or any(x in ("", ".", "..") for x in p.parts):
        raise DonorError(f"unsafe OBB path: {ue3_name!r}")
    return Path(*p.parts)


def read_ue3_obb_index(path: Path):
    size = path.stat().st_size
    entries = []
    with path.open("rb") as f:
        magic = f.read(len(OBB_MAGIC))
        if magic != OBB_MAGIC:
            raise DonorError(f"unsupported OBB magic: {magic!r}")
        preamble = f.read(len(OBB_PREAMBLE))
        if preamble != OBB_PREAMBLE:
            raise DonorError(f"unsupported UE3 OBB preamble: {preamble.hex()}")

        first_payload = None
        while first_payload is None or f.tell() < first_payload:
            raw = f.read(4)
            if len(raw) != 4:
                raise DonorError("truncated OBB index")
            (name_len,) = struct.unpack("<I", raw)
            if not 2 <= name_len <= 4096:
                raise DonorError(
                    f"invalid OBB filename length {name_len} at 0x{f.tell()-4:x}"
                )
            raw_name = f.read(name_len)
            if len(raw_name) != name_len or raw_name[-1:] != b"\0":
                raise DonorError("invalid/truncated OBB filename")
            try:
                name = raw_name[:-1].decode("utf-8")
            except UnicodeDecodeError as e:
                raise DonorError("non-UTF-8 OBB filename") from e
            tail = f.read(12)
            if len(tail) != 12:
                raise DonorError("truncated OBB index record")
            offset, payload_size = struct.unpack("<QI", tail)
            if first_payload is None:
                first_payload = offset
            entries.append((name, offset, payload_size))

        if first_payload != f.tell():
            raise DonorError(
                f"OBB index/payload boundary mismatch: "
                f"index=0x{f.tell():x}, payload=0x{first_payload:x}"
            )

    if not entries:
        raise DonorError("empty OBB index")
    previous_end = entries[0][1]
    if previous_end != first_payload:
        raise DonorError("invalid first OBB payload offset")
    for name, offset, payload_size in entries:
        if offset != previous_end:
            raise DonorError(
                f"non-contiguous OBB payload at {name!r}: {offset} != {previous_end}"
            )
        end = offset + payload_size
        if end > size:
            raise DonorError(f"OBB payload outside file: {name!r}")
        previous_end = end
    if previous_end != size:
        raise DonorError(
            f"OBB has trailing or missing data: last={previous_end}, size={size}"
        )
    return entries


def extract_ue3_obb(path: Path, out: Path) -> list[Path]:
    entries = read_ue3_obb_index(path)
    written = []
    with path.open("rb") as src:
        for name, offset, payload_size in entries:
            rel = safe_relpath(name)
            dst = out / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            src.seek(offset)
            remaining = payload_size
            with dst.open("wb") as fp:
                while remaining:
                    chunk = src.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise DonorError(f"short read extracting {name!r}")
                    fp.write(chunk)
                    remaining -= len(chunk)
            written.append(dst)
    return written


def find_xapk_members(z: zipfile.ZipFile):
    names = set(z.namelist())
    manifest = None
    if "manifest.json" in names:
        manifest = json.loads(z.read("manifest.json"))
        package = manifest.get("package_name") or manifest.get("packageName")
        version_code = manifest.get("version_code") or manifest.get("versionCode")
        if package and package != PACKAGE:
            raise DonorError(f"wrong package: {package}")
        if version_code is not None and int(version_code) != VERSION_CODE:
            raise DonorError(f"wrong version code: {version_code}")

    apk_candidates = [n for n in names if n.lower().endswith(".apk")]
    obb_candidates = [
        n for n in names if n.endswith("/" + OBB_NAME) or n == OBB_NAME
    ]
    if not apk_candidates:
        raise DonorError("XAPK contains no APK")
    if not obb_candidates:
        raise DonorError(f"XAPK does not contain {OBB_NAME}")
    exact = f"{PACKAGE}.apk"
    apk = exact if exact in names else sorted(apk_candidates)[0]
    return apk, sorted(obb_candidates)[0], manifest


def extract_native_apk(apk: Path, out: Path) -> Path:
    with zipfile.ZipFile(apk) as z:
        try:
            info = z.getinfo(NATIVE_PATH)
        except KeyError as e:
            raise DonorError(f"APK missing {NATIVE_PATH}") from e
        dst = out / NATIVE_PATH
        dst.parent.mkdir(parents=True, exist_ok=True)
        with z.open(info) as src, dst.open("wb") as fp:
            shutil.copyfileobj(src, fp, 1024 * 1024)
        with dst.open("rb") as fp:
            digest = sha256_stream(fp)
        if digest != NATIVE_SHA256:
            raise DonorError(f"unexpected libUnrealEngine3.so SHA-256: {digest}")

        for member in ("assets/UE3CommandLine.txt",):
            try:
                info = z.getinfo(member)
            except KeyError:
                continue
            target = out / member
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, target.open("wb") as fp:
                shutil.copyfileobj(src, fp)
    return dst


def import_xapk(xapk: Path, destination: Path) -> None:
    destination = destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(prefix=destination.name + ".staging-", dir=destination.parent)
    )
    try:
        with zipfile.ZipFile(xapk) as z:
            apk_member, obb_member, _ = find_xapk_members(z)
            scratch = staging / ".donor"
            scratch.mkdir()
            apk = scratch / "EpicCitadel.apk"
            obb_dir = staging / "obb"
            obb_dir.mkdir()
            obb = obb_dir / OBB_NAME
            with z.open(apk_member) as src, apk.open("wb") as fp:
                shutil.copyfileobj(src, fp, 1024 * 1024)
            with z.open(obb_member) as src, obb.open("wb") as fp:
                shutil.copyfileobj(src, fp, 1024 * 1024)

        extract_native_apk(apk, staging)
        # Keep the original OBB. FFileManagerAndroid natively opens the path
        # returned by JavaCallback_GetMainAPKExpansionName and parses this same
        # UE3AndroidOBB table, which is a lower-risk runtime path than replacing
        # Epic's file manager. The expanded tree remains useful for diagnostics
        # and for a future no-container mode.
        extracted = extract_ue3_obb(obb, staging)
        if not (staging / "UDKGame" / "AndroidTOC.txt").is_file():
            raise DonorError(
                "OBB extraction did not produce UDKGame/AndroidTOC.txt"
            )
        if not (
            staging / "UDKGame" / "CookedAndroid" / "EpicCitadel.xxx"
        ).is_file():
            raise DonorError("OBB extraction did not produce EpicCitadel.xxx")

        shutil.rmtree(scratch)
        marker = {
            "package": PACKAGE,
            "version_name": VERSION_NAME,
            "version_code": VERSION_CODE,
            "native_sha256": NATIVE_SHA256,
            "obb": OBB_NAME,
            "obb_path": f"obb/{OBB_NAME}",
            "obb_entries": len(extracted),
        }
        (staging / ".open-citadel-donor.json").write_text(
            json.dumps(marker, indent=2) + "\n"
        )

        if destination.exists():
            backup = destination.with_name(destination.name + ".old")
            if backup.exists():
                shutil.rmtree(backup)
            destination.rename(backup)
            try:
                staging.rename(destination)
            except Exception:
                backup.rename(destination)
                raise
            shutil.rmtree(backup)
        else:
            staging.rename(destination)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def inspect_xapk(xapk: Path) -> dict:
    with zipfile.ZipFile(xapk) as z:
        apk_member, obb_member, manifest = find_xapk_members(z)
        with tempfile.TemporaryDirectory() as td:
            obb = Path(td) / OBB_NAME
            with z.open(obb_member) as src, obb.open("wb") as fp:
                shutil.copyfileobj(src, fp, 1024 * 1024)
            entries = read_ue3_obb_index(obb)
        return {
            "package": PACKAGE,
            "version_name": VERSION_NAME,
            "version_code": VERSION_CODE,
            "apk_member": apk_member,
            "obb_member": obb_member,
            "obb_entries": len(entries),
            "first_payload_offset": entries[0][1],
            "obb_size": z.getinfo(obb_member).file_size,
            "manifest": manifest,
        }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("donor", type=Path, help="Epic Citadel 1.07 XAPK")
    ap.add_argument("destination", type=Path, nargs="?")
    ap.add_argument("--inspect", action="store_true")
    args = ap.parse_args()
    try:
        if args.inspect:
            print(json.dumps(inspect_xapk(args.donor), indent=2, sort_keys=True))
            return 0
        if args.destination is None:
            ap.error("destination is required unless --inspect is used")
        import_xapk(args.donor, args.destination)
        return 0
    except (
        DonorError,
        OSError,
        zipfile.BadZipFile,
        json.JSONDecodeError,
    ) as e:
        print(f"open-citadel import failed: {e}", file=os.sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
