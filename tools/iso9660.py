"""Read and selectively extract files from an ISO9660 image."""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import struct
import sys
from typing import BinaryIO, Iterator

SECTOR_SIZE = 2048


@dataclasses.dataclass(frozen=True)
class Entry:
    path: str
    lba: int
    size: int
    is_dir: bool


class Iso9660:
    def __init__(self, path: pathlib.Path):
        self.path = path
        self._file: BinaryIO = path.open("rb")
        self._image_size = path.stat().st_size
        self._root = self._find_root()

    def close(self) -> None:
        self._file.close()

    def __enter__(self) -> Iso9660:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _find_root(self) -> Entry:
        for sector in range(16, 80):
            self._file.seek(sector * SECTOR_SIZE)
            descriptor = self._file.read(SECTOR_SIZE)
            if len(descriptor) != SECTOR_SIZE or descriptor[1:6] != b"CD001":
                raise ValueError("ISO9660 volume descriptor not found")
            if descriptor[0] == 255:
                break
            if descriptor[0] == 1:
                if descriptor[6] != 1:
                    raise ValueError("Unsupported ISO9660 descriptor version")
                root = self._parse_record(descriptor[156:190], "", allow_special=True)
                if root is None or not root.is_dir:
                    raise ValueError("Invalid ISO9660 root directory")
                return root
        raise ValueError("ISO9660 primary volume descriptor not found")

    @staticmethod
    def _parse_record(data: bytes, parent: str, allow_special: bool = False) -> Entry | None:
        if not data or data[0] == 0:
            return None
        record_size = data[0]
        if record_size < 34 or record_size > len(data):
            raise ValueError("Invalid ISO9660 directory record")
        name_size = data[32]
        if 33 + name_size > record_size:
            raise ValueError("Invalid ISO9660 file identifier")
        name_bytes = data[33 : 33 + name_size]
        if name_bytes in (b"\x00", b"\x01"):
            if not allow_special:
                return None
            name = ""
        else:
            name = name_bytes.decode("ascii", errors="replace")
        if ";" in name:
            name = name.rsplit(";", 1)[0]
        name = name.rstrip(".")
        full_path = f"{parent}/{name}" if parent and name else parent or name
        return Entry(
            full_path,
            struct.unpack_from("<I", data, 2)[0],
            struct.unpack_from("<I", data, 10)[0],
            bool(data[25] & 2),
        )

    def _children(self, directory: Entry) -> Iterator[Entry]:
        start = directory.lba * SECTOR_SIZE
        end = start + directory.size
        if end > self._image_size:
            raise ValueError(f"Directory is outside the ISO image: {directory.path}")
        self._file.seek(start)
        data = self._file.read(directory.size)
        offset = 0
        while offset < len(data):
            record_size = data[offset]
            if record_size == 0:
                offset = (offset // SECTOR_SIZE + 1) * SECTOR_SIZE
                continue
            entry = self._parse_record(data[offset : offset + record_size], directory.path)
            if entry is not None:
                yield entry
            offset += record_size

    def entries(self) -> Iterator[Entry]:
        pending = [self._root]
        visited: set[tuple[int, int]] = set()
        while pending:
            directory = pending.pop()
            key = (directory.lba, directory.size)
            if key in visited:
                continue
            visited.add(key)
            children = list(self._children(directory))
            for entry in children:
                yield entry
                if entry.is_dir:
                    pending.append(entry)

    def find(self, path: str) -> Entry:
        wanted = path.replace("\\", "/").strip("/").casefold()
        for entry in self.entries():
            if entry.path.casefold() == wanted:
                if entry.is_dir:
                    raise ValueError(f"Path is a directory: {path}")
                if entry.lba * SECTOR_SIZE + entry.size > self._image_size:
                    raise ValueError(f"File is outside the ISO image: {entry.path}")
                return entry
        raise FileNotFoundError(f"File not found in ISO: {path}")

    def read_file(self, entry: Entry) -> bytes:
        if entry.is_dir:
            raise ValueError(f"Path is a directory: {entry.path}")
        if entry.lba * SECTOR_SIZE + entry.size > self._image_size:
            raise ValueError(f"File is outside the ISO image: {entry.path}")
        self._file.seek(entry.lba * SECTOR_SIZE)
        data = self._file.read(entry.size)
        if len(data) != entry.size:
            raise ValueError(f"Unexpected end of ISO while reading {entry.path}")
        return data

    def extract(self, entry: Entry, output: pathlib.Path) -> None:
        output.parent.mkdir(parents=True, exist_ok=True)
        self._file.seek(entry.lba * SECTOR_SIZE)
        remaining = entry.size
        with output.open("wb") as destination:
            while remaining:
                chunk = self._file.read(min(remaining, 1024 * 1024))
                if not chunk:
                    raise ValueError(f"Unexpected end of ISO while reading {entry.path}")
                destination.write(chunk)
                remaining -= len(chunk)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="list ISO paths and file sizes")
    list_parser.add_argument("iso", type=pathlib.Path)

    extract_parser = subparsers.add_parser("extract", help="extract one file by ISO path")
    extract_parser.add_argument("iso", type=pathlib.Path)
    extract_parser.add_argument("iso_path", help="path inside the image")
    extract_parser.add_argument("output", type=pathlib.Path)

    args = parser.parse_args()
    try:
        with Iso9660(args.iso) as image:
            if args.command == "list":
                for entry in sorted(image.entries(), key=lambda item: item.path.casefold()):
                    kind = "DIR " if entry.is_dir else "FILE"
                    print(f"{kind} {entry.size:>10}  /{entry.path}")
            else:
                entry = image.find(args.iso_path)
                image.extract(entry, args.output)
                print(f"Extracted /{entry.path} ({entry.size} bytes) to {args.output}")
    except (OSError, ValueError, FileNotFoundError) as error:
        print(f"iso9660: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())