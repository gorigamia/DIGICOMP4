"""Inspect an ELF32 executable stored in an ISO9660 image."""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

from iso9660 import Iso9660

PT_LOAD = 1
PF_X = 1


class Elf32:
    def __init__(self, data: bytes):
        if len(data) < 52 or data[:4] != b"\x7fELF":
            raise ValueError("Not an ELF file")
        if data[4] != 1 or data[5] != 1 or data[6] != 1:
            raise ValueError("Expected a 32-bit little-endian ELF file")

        self.data = data
        self.file_type, self.machine = struct.unpack_from("<HH", data, 16)
        self.entry = struct.unpack_from("<I", data, 24)[0]
        phoff = struct.unpack_from("<I", data, 28)[0]
        shoff = struct.unpack_from("<I", data, 32)[0]
        phentsize, phnum = struct.unpack_from("<HH", data, 42)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)

        if phnum and (phentsize < 32 or phoff + phentsize * phnum > len(data)):
            raise ValueError("Invalid ELF program header table")
        self.segments = [
            struct.unpack_from("<8I", data, phoff + i * phentsize)
            for i in range(phnum)
        ]
        for segment in self.segments:
            if segment[1] + segment[4] > len(data):
                raise ValueError("ELF segment exceeds file size")

        self.section_names = self._read_section_names(shoff, shentsize, shnum, shstrndx)

    def _read_section_names(self, offset: int, entry_size: int, count: int, names_index: int) -> list[str]:
        if not count:
            return []
        if entry_size < 40 or offset + entry_size * count > len(self.data) or names_index >= count:
            raise ValueError("Invalid ELF section header table")
        sections = [
            struct.unpack_from("<10I", self.data, offset + i * entry_size)
            for i in range(count)
        ]
        names_section = sections[names_index]
        start, size = names_section[4], names_section[5]
        if start + size > len(self.data):
            raise ValueError("ELF section name table exceeds file size")
        names = self.data[start : start + size]
        result = []
        for section in sections:
            name_offset = section[0]
            if name_offset >= len(names):
                result.append("")
                continue
            end = names.find(b"\0", name_offset)
            if end < 0:
                result.append("")
                continue
            result.append(names[name_offset:end].decode("ascii", errors="replace"))
        return result

    def entry_is_executable(self) -> bool:
        return any(
            segment[0] == PT_LOAD
            and segment[6] & PF_X
            and segment[2] <= self.entry < segment[2] + segment[5]
            for segment in self.segments
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=pathlib.Path)
    parser.add_argument("iso_path", help="path to the ELF file inside the ISO")
    args = parser.parse_args()

    try:
        with Iso9660(args.iso) as image:
            entry = image.find(args.iso_path)
            elf = Elf32(image.read_file(entry))
        print(f"ELF32 little-endian: type={elf.file_type}, machine={elf.machine}, entry=0x{elf.entry:08X}")
        if not elf.entry_is_executable():
            raise ValueError("ELF entrypoint is not inside an executable PT_LOAD segment")
        for segment in elf.segments:
            if segment[0] != PT_LOAD:
                continue
            flags = "".join(letter for bit, letter in ((PF_X, "X"), (2, "W"), (4, "R")) if segment[6] & bit)
            print(
                f"PT_LOAD vaddr=0x{segment[2]:08X} file=0x{segment[1]:08X} "
                f"filesz=0x{segment[4]:X} memsz=0x{segment[5]:X} flags={flags or '-'}"
            )
        named_sections = [name for name in elf.section_names if name]
        print("Named sections: " + (", ".join(named_sections) if named_sections else "none"))
    except (OSError, ValueError, FileNotFoundError) as error:
        print(f"inspect_elf: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())