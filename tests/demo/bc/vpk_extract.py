#!/usr/bin/env python3
"""List or extract files from a Source VPK without third-party modules."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

VPK_SIGNATURE = 0x55AA1234
ENTRY_TERMINATOR = 0xFFFF


@dataclass(frozen=True)
class Entry:
    extension: str
    directory: str
    name: str
    crc: int
    preload: bytes
    archive: int
    offset: int
    length: int

    @property
    def path(self) -> str:
        parts = [part for part in (self.directory, self.name) if part]
        return "/".join(parts) + "." + self.extension


class Vpk:
    def __init__(self, directory_vpk: Path):
        self.path = directory_vpk.resolve()
        self.data = self.path.read_bytes()
        if len(self.data) < 12:
            raise ValueError("VPK header is truncated")
        signature, version, tree_size = struct.unpack_from("<III", self.data)
        if signature != VPK_SIGNATURE:
            raise ValueError("not a Source VPK directory file")
        if version not in (1, 2):
            raise ValueError(f"unsupported VPK version {version}")
        header_size = 12 if version == 1 else 28
        if len(self.data) < header_size + tree_size:
            raise ValueError("VPK directory tree is truncated")
        self.tree_end = header_size + tree_size
        self.entries = self._parse_tree(header_size)

    def _parse_tree(self, offset: int) -> list[Entry]:
        entries: list[Entry] = []

        def read_string(position: int) -> tuple[str, int]:
            end = self.data.find(b"\0", position, self.tree_end)
            if end < 0:
                raise ValueError("unterminated VPK path string")
            return self.data[position:end].decode("utf-8", "surrogateescape"), end + 1

        position = offset
        while True:
            extension, position = read_string(position)
            if not extension:
                break
            while True:
                directory, position = read_string(position)
                if not directory:
                    break
                while True:
                    name, position = read_string(position)
                    if not name:
                        break
                    if position + 18 > self.tree_end:
                        raise ValueError("VPK entry metadata is truncated")
                    crc, preload_size, archive, entry_offset, entry_length = struct.unpack_from(
                        "<IHHII", self.data, position
                    )
                    terminator = struct.unpack_from("<H", self.data, position + 16)[0]
                    if terminator != ENTRY_TERMINATOR:
                        raise ValueError(f"invalid VPK entry terminator 0x{terminator:04x}")
                    position += 18
                    if position + preload_size > self.tree_end:
                        entry_path = "/".join(part for part in (directory, name) if part) + "." + extension
                        raise ValueError(
                            f"VPK preload data is truncated for {entry_path!r} "
                            f"at 0x{position:x} + {preload_size}"
                        )
                    preload = self.data[position : position + preload_size]
                    position += preload_size
                    entries.append(
                        Entry(
                            extension=extension,
                            directory=directory,
                            name=name,
                            crc=crc,
                            preload=preload,
                            archive=archive,
                            offset=entry_offset,
                            length=entry_length,
                        )
                    )
        return entries

    def find(self, name: str) -> Entry:
        matches = [entry for entry in self.entries if entry.path == name]
        if not matches:
            raise KeyError(f"{name!r} is not in {self.path}")
        if len(matches) != 1:
            raise ValueError(f"{name!r} has {len(matches)} VPK entries")
        return matches[0]

    def archive_path(self, entry: Entry) -> Path:
        if entry.archive == ENTRY_TERMINATOR:
            return self.path
        suffix = "_dir.vpk"
        if not self.path.name.endswith(suffix):
            raise ValueError(f"cannot derive archive name from {self.path}")
        return self.path.with_name(self.path.name[: -len(suffix)] + f"_{entry.archive:03d}.vpk")

    def read(self, entry: Entry) -> bytes:
        archive = self.archive_path(entry)
        if entry.archive == ENTRY_TERMINATOR:
            start = self.tree_end
            source = self.data
        else:
            source = archive.read_bytes()
            start = entry.offset
        end = start + entry.length
        if end > len(source):
            raise ValueError(f"{entry.path!r} extends past {archive}")
        payload = entry.preload + source[start:end]
        import zlib

        if zlib.crc32(payload) & 0xFFFFFFFF != entry.crc:
            raise ValueError(f"CRC mismatch for {entry.path!r}")
        return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory_vpk", type=Path)
    parser.add_argument("--list", action="store_true", help="list matching entries")
    parser.add_argument("--match", default="", help="substring used with --list")
    parser.add_argument("--extract", help="exact VPK path to extract")
    parser.add_argument("--output", type=Path, help="output path for --extract")
    args = parser.parse_args()

    vpk = Vpk(args.directory_vpk)
    if args.list:
        needle = args.match.lower()
        for entry in sorted(vpk.entries, key=lambda item: item.path.lower()):
            if needle in entry.path.lower():
                print(f"{entry.path}\t{entry.archive}\t{entry.offset}\t{entry.length}")
        return 0
    if not args.extract or not args.output:
        parser.error("--extract and --output are required unless --list is used")
    payload = vpk.read(vpk.find(args.extract))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f"{args.output}\t{len(payload)}\t{hashlib.sha256(payload).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
