#!/usr/bin/env python3
"""
premium_catalog_draft.py - Tankoyomi-Premium catalog entry drafter.

Usage:
  premium_catalog_draft.py --torrent-file path/to/series.torrent \
                           --series-id berserk \
                           --title "Berserk" \
                           --status completed \
                           --uploader 1r0n \
                           --release-edition "VIZ Digital" \
                           --mapping mapping.csv \
                           --out berserk.draft.json

  premium_catalog_draft.py --magnet "magnet:?xt=urn:btih:..." \
                           --series-id one_piece \
                           --title "One Piece" \
                           --status ongoing \
                           --post-coverage-slug one-piece \
                           --post-coverage-after-vol 109 \
                           --uploader 1r0n \
                           --out one_piece.draft.json

Output is a draft entry that still needs:
  1. Hand-verified volume/file mapping (the helper guesses from filename
     patterns like "Berserk v01 [...].cbz" but cannot always tell).
  2. Optional pageCount values per volume (run pagecount-probe later).
  3. Optional coverPageHint per volume (defaults to empty -> first-image
     fallback at runtime).

Never auto-commits. The curator reviews + edits the draft before merging into
the bundled catalog file.

Per Codex brainstorm section 25 + section 27.5 the dominant per-series cost
is file-mapping verification; this helper eliminates the data-entry portion
of that cost without removing the verification step.
"""
from __future__ import annotations

import argparse
import csv
import dataclasses
import hashlib
import json
import re
import sys
import urllib.parse
from pathlib import Path
from typing import Iterable

# --------------------------------------------------------------------------- #
# Bencode decoder. Self-contained, no external deps. Just enough to parse a
# .torrent's info dict and compute infoHash. Based on the BEP-3 spec.
# --------------------------------------------------------------------------- #

class BencodeError(Exception):
    pass

def _decode(data: bytes, pos: int):
    if pos >= len(data):
        raise BencodeError("unexpected EOF")
    c = data[pos:pos+1]
    if c.isdigit():
        colon = data.index(b":", pos)
        length = int(data[pos:colon])
        start = colon + 1
        end = start + length
        return data[start:end], end
    if c == b"i":
        end = data.index(b"e", pos)
        return int(data[pos+1:end]), end + 1
    if c == b"l":
        out = []
        pos += 1
        while data[pos:pos+1] != b"e":
            value, pos = _decode(data, pos)
            out.append(value)
        return out, pos + 1
    if c == b"d":
        out = {}
        pos += 1
        while data[pos:pos+1] != b"e":
            key, pos = _decode(data, pos)
            value, pos = _decode(data, pos)
            out[key] = value
        return out, pos + 1
    raise BencodeError(f"unexpected byte at {pos}: {c!r}")

def bdecode(data: bytes):
    value, end = _decode(data, 0)
    if end != len(data):
        raise BencodeError("trailing bytes after decode")
    return value

def _encode(value, out: bytearray):
    if isinstance(value, int):
        out.extend(f"i{value}e".encode())
    elif isinstance(value, bytes):
        out.extend(f"{len(value)}:".encode())
        out.extend(value)
    elif isinstance(value, str):
        b = value.encode("utf-8")
        out.extend(f"{len(b)}:".encode())
        out.extend(b)
    elif isinstance(value, list):
        out.append(ord("l"))
        for v in value:
            _encode(v, out)
        out.append(ord("e"))
    elif isinstance(value, dict):
        out.append(ord("d"))
        # Keys MUST be sorted lexicographically per BEP-3.
        for k in sorted(value.keys()):
            if isinstance(k, str):
                kb = k.encode("utf-8")
            else:
                kb = k
            out.extend(f"{len(kb)}:".encode())
            out.extend(kb)
            _encode(value[k], out)
        out.append(ord("e"))
    else:
        raise BencodeError(f"cannot encode {type(value)}")

def bencode(value) -> bytes:
    out = bytearray()
    _encode(value, out)
    return bytes(out)

# --------------------------------------------------------------------------- #
# Torrent parsing.
# --------------------------------------------------------------------------- #

@dataclasses.dataclass
class TorrentFile:
    index: int
    path: str       # e.g. "Berserk v01 [1r0n].cbz" (joined from path segments)
    size: int       # bytes
    piece_start: int
    piece_end: int  # inclusive

@dataclasses.dataclass
class TorrentMeta:
    info_hash: str  # 40-char lowercase hex
    name: str
    piece_length: int
    files: list[TorrentFile]
    creation_date: int | None

def _bytes_to_str(v) -> str:
    if isinstance(v, bytes):
        try:
            return v.decode("utf-8")
        except UnicodeDecodeError:
            return v.decode("latin1", errors="replace")
    return str(v)

def parse_torrent(path: Path) -> TorrentMeta:
    raw = path.read_bytes()
    root = bdecode(raw)
    if not isinstance(root, dict) or b"info" not in root:
        raise BencodeError("not a torrent: missing 'info' dict")
    info = root[b"info"]
    info_hash = hashlib.sha1(bencode(info)).hexdigest()
    name = _bytes_to_str(info.get(b"name", b""))
    piece_length = int(info[b"piece length"])
    if piece_length <= 0:
        raise BencodeError(f"invalid piece length: {piece_length}")

    files: list[TorrentFile] = []
    if b"files" in info:
        # Multi-file torrent (the common case for series packs).
        offset = 0
        for idx, f in enumerate(info[b"files"]):
            size = int(f[b"length"])
            if size < 0:
                raise BencodeError(f"invalid file length for index {idx}: {size}")
            segments = [_bytes_to_str(s) for s in f[b"path"]]
            file_path = "/".join(segments)
            piece_start = offset // piece_length
            piece_end = (offset + size - 1) // piece_length
            files.append(TorrentFile(idx, file_path, size, piece_start, piece_end))
            offset += size
    else:
        # Single-file torrent (uncommon for series packs).
        size = int(info[b"length"])
        files.append(TorrentFile(0, name, size, 0, max(0, (size - 1) // piece_length)))

    creation_date = int(root[b"creation date"]) if b"creation date" in root else None
    return TorrentMeta(info_hash=info_hash, name=name, piece_length=piece_length,
                       files=files, creation_date=creation_date)

# --------------------------------------------------------------------------- #
# Volume guesser. Tries to extract "vNN" or "Vol NN" from a filename.
# --------------------------------------------------------------------------- #

_VOL_PATTERNS = [
    re.compile(r"\bv(\d{1,3})\b", re.IGNORECASE),
    re.compile(r"\bvol(?:ume)?\s*\.?\s*(\d{1,3})\b", re.IGNORECASE),
    re.compile(r"\s-\s*(\d{1,3})\s*-", re.IGNORECASE),
]

def guess_volume(filename: str) -> int | None:
    for rx in _VOL_PATTERNS:
        m = rx.search(filename)
        if m:
            try:
                return int(m.group(1))
            except ValueError:
                continue
    return None

# --------------------------------------------------------------------------- #
# Mapping CSV loader.
# --------------------------------------------------------------------------- #

@dataclasses.dataclass
class ChapterMapping:
    vol: int
    chapter_num: str
    chapter_title: str

def load_mapping(path: Path) -> dict[int, list[ChapterMapping]]:
    out: dict[int, list[ChapterMapping]] = {}
    with path.open("r", encoding="utf-8") as fh:
        reader = csv.reader(fh)
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            if len(row) < 3:
                continue
            try:
                vol = int(row[0])
            except ValueError:
                continue
            chapter_num = row[1].strip()
            chapter_title = row[2].strip().strip('"')
            out.setdefault(vol, []).append(
                ChapterMapping(vol, chapter_num, chapter_title))
    return out

# --------------------------------------------------------------------------- #
# Magnet parsing.
# --------------------------------------------------------------------------- #

def info_hash_from_magnet(magnet: str) -> str:
    parsed = urllib.parse.urlparse(magnet)
    qs = urllib.parse.parse_qs(parsed.query)
    for xt in qs.get("xt", []):
        if xt.lower().startswith("urn:btih:"):
            ih = xt.split(":")[-1]
            if len(ih) == 40 and all(c in "0123456789abcdefABCDEF" for c in ih):
                return ih.lower()
    raise SystemExit("Could not extract a 40-char hex infoHash from magnet xt= parameter.")

# --------------------------------------------------------------------------- #
# Draft builder.
# --------------------------------------------------------------------------- #

def build_draft(series_id: str, title: str, status: str,
                magnet: str, info_hash: str, uploader: str, release_edition: str,
                files: list[TorrentFile] | None,
                mapping: dict[int, list[ChapterMapping]],
                post_coverage_slug: str, post_coverage_after_vol: int) -> dict:
    volumes_out = []
    if files is not None:
        for f in files:
            if not f.path.lower().endswith(".cbz"):
                # Helper flags non-cbz entries; the curator can include or
                # drop them. Real catalogs reject anything but cbz.
                volumes_out.append({
                    "_warning": f"file index {f.index} is not .cbz: {f.path}",
                    "fileIndex": f.index,
                    "fileSizeBytes": f.size,
                    "pieceStart": f.piece_start,
                    "pieceEnd": f.piece_end,
                    "cbzFileName": f.path.split("/")[-1],
                })
                continue
            vol = guess_volume(f.path)
            chapters = []
            if vol is not None and vol in mapping:
                chapters = [{"num": c.chapter_num, "title": c.chapter_title}
                            for c in mapping[vol]]
            volumes_out.append({
                "vol": vol if vol is not None else 0,
                "fileIndex": f.index,
                "fileSizeBytes": f.size,
                "pieceStart": f.piece_start,
                "pieceEnd": f.piece_end,
                "cbzFileName": f.path.split("/")[-1],
                "boundaryPolicy": "allow-piece-overlap",
                "pageCount": 0,
                "coverPageHint": "",
                "chapters": chapters,
                "_needs_review": vol is None,
            })
    else:
        volumes_out.append({
            "_TODO_resolve_files": True,
            "_note": "Magnet-only mode: re-run with --torrent-file to populate volumes."
        })

    return {
        "seriesId": series_id,
        "title": title,
        "alternateTitles": [],
        "anilistId": 0,
        "status": status,
        "magnetUri": magnet,
        "expectedInfoHash": info_hash,
        "trustedUploader": uploader,
        "releaseEdition": release_edition,
        "format": "one-cbz-per-volume",
        "volumes": volumes_out,
        "postCoverageFallback": {
            "weebcentralSlug": post_coverage_slug,
            "startsAfterVolume": post_coverage_after_vol,
        },
    }

# --------------------------------------------------------------------------- #
# CLI.
# --------------------------------------------------------------------------- #

def main() -> int:
    ap = argparse.ArgumentParser(description="Tankoyomi-Premium catalog entry drafter.")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--torrent-file", type=Path, help="Path to a .torrent file (preferred).")
    src.add_argument("--magnet", type=str, help="magnet:?xt=urn:btih:... URI (partial draft only).")

    ap.add_argument("--series-id", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--status", choices=["completed", "ongoing"], required=True)
    ap.add_argument("--uploader", required=True)
    ap.add_argument("--release-edition", default="")
    ap.add_argument("--mapping", type=Path, help="CSV: vol,chapter_num,chapter_title")
    ap.add_argument("--post-coverage-slug", default="", help="WeebCentral slug for chapters past coverage.")
    ap.add_argument("--post-coverage-after-vol", type=int, default=0)
    ap.add_argument("--out", type=Path, required=True)

    args = ap.parse_args()

    if args.torrent_file is not None:
        meta = parse_torrent(args.torrent_file)
        info_hash = meta.info_hash
        # If user didn't pass a magnet but we have a torrent, synthesize a minimal
        # magnet for the catalog. The runtime needs the xt portion only.
        synthesized_magnet = f"magnet:?xt=urn:btih:{info_hash}&dn={urllib.parse.quote(meta.name)}"
        magnet = synthesized_magnet
        files = meta.files
    else:
        info_hash = info_hash_from_magnet(args.magnet)
        magnet = args.magnet
        files = None

    mapping = load_mapping(args.mapping) if args.mapping is not None else {}

    draft = build_draft(
        series_id=args.series_id,
        title=args.title,
        status=args.status,
        magnet=magnet,
        info_hash=info_hash,
        uploader=args.uploader,
        release_edition=args.release_edition,
        files=files,
        mapping=mapping,
        post_coverage_slug=args.post_coverage_slug,
        post_coverage_after_vol=args.post_coverage_after_vol,
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(draft, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"Wrote draft: {args.out}")
    print(f"  infoHash: {info_hash}")
    if files is not None:
        print(f"  files: {len(files)} ({sum(1 for f in files if f.path.endswith('.cbz'))} cbz)")
        # Surface volumes that need review.
        for v in draft["volumes"]:
            if v.get("_needs_review"):
                print(f"  REVIEW: file {v['fileIndex']} '{v['cbzFileName']}' - could not guess volume number")
            if "_warning" in v:
                print(f"  WARN:   {v['_warning']}")
    else:
        print("  files: 0 (magnet-only mode; re-run with --torrent-file for full draft)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
