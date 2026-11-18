"""Generate C and relocated images for IRX modules found in a PS2 ISO."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

from iso9660 import Iso9660

ROOT = pathlib.Path(__file__).resolve().parents[1]


def _candidate_reference_tools() -> list[pathlib.Path]:
    candidates = [ROOT / "tools" / "reference", ROOT / "Grandia3-PC-main" / "tools"]
    env = os.environ.get("DIGIMON_RECOMP_REFERENCE_TOOLS") or os.environ.get("REFERENCE_TOOLS")
    if env:
        candidates.insert(0, pathlib.Path(env))
    seen: set[pathlib.Path] = set()
    result = []
    for candidate in candidates:
        resolved = candidate.resolve() if candidate.exists() else candidate
        if resolved in seen:
            continue
        seen.add(resolved)
        result.append(candidate)
    return result


def find_reference_tools() -> pathlib.Path:
    for candidate in _candidate_reference_tools():
        if candidate.is_dir():
            return candidate
    searched = ", ".join(str(path) for path in _candidate_reference_tools())
    raise FileNotFoundError(f"Reference tools not found. Searched: {searched}")


def load_irx_type(reference_tools: pathlib.Path) -> type:
    sys.path.insert(0, str(reference_tools))
    from iop_recomp import IRX

    return IRX


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "build" / "generated" / "iop")
    parser.add_argument("--base", type=lambda value: int(value, 0), default=0x00100000)
    args = parser.parse_args()

    try:
        reference_tools = find_reference_tools()
        iop_recompiler = reference_tools / "iop_recomp.py"
        IRX = load_irx_type(reference_tools)
        args.output.mkdir(parents=True, exist_ok=True)
        manifest = []
        cursor = args.base
        with Iso9660(args.iso) as image, tempfile.TemporaryDirectory() as temp_dir:
            temp_root = pathlib.Path(temp_dir)
            modules = sorted(
                (entry for entry in image.entries() if entry.path.upper().endswith(".IRX")),
                key=lambda entry: entry.path.casefold(),
            )
            if not modules:
                raise ValueError("No IRX modules found in the ISO")

            for index, entry in enumerate(modules):
                prefix = re.sub(r"[^a-zA-Z0-9_]", "_", pathlib.PurePosixPath(entry.path).stem).lower()
                irx_path = temp_root / f"{index}_{prefix}.irx"
                irx_path.write_bytes(image.read_file(entry))
                irx = IRX(str(irx_path), cursor)
                output_dir = args.output / prefix
                subprocess.run(
                    [sys.executable, str(iop_recompiler), str(irx_path), hex(cursor), str(output_dir), prefix],
                    check=True,
                )
                manifest.append({
                    "iso_path": entry.path,
                    "prefix": prefix,
                    "base": cursor,
                    "entry": irx.entry,
                    "mem_size": irx.memsz,
                    "text_size": irx.text_size,
                    "data_size": irx.data_size,
                    "bss_size": irx.bss_size,
                })
                cursor = align(cursor + irx.memsz, 0x1000)

        (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")
        generated = sorted(path.relative_to(args.output).as_posix() for path in args.output.rglob("*.c"))
        (args.output / "files.txt").write_text("\n".join(generated) + "\n", encoding="ascii")
        registry = [
            '#include "iop/game_modules.h"',
            "",
            'extern "C" {',
        ]
        for module in manifest:
            prefix = module["prefix"]
            registry.extend((
                f"extern const IOPFuncEntry {prefix}_functable[];",
                f"extern const unsigned {prefix}_functable_count;",
                f"extern const uint32_t {prefix}_base, {prefix}_entry, {prefix}_gp;",
                f"extern const uint32_t {prefix}_memsz, {prefix}_filesz;",
                f"extern const uint8_t {prefix}_image[];",
            ))
        registry.extend(("}", "", "extern const GameIopModule g_game_iop_modules[] = {"))
        for module in manifest:
            prefix = module["prefix"]
            registry.append(
                f'    {{ "{module["iso_path"]}", {prefix}_base, {prefix}_entry, {prefix}_gp, '
                f"{prefix}_memsz, {prefix}_filesz, {prefix}_image, {prefix}_functable, "
                f"{prefix}_functable_count }},"
            )
        registry.extend(("};", f"extern const unsigned g_game_iop_module_count = {len(manifest)};"))
        (args.output / "module_registry.cpp").write_text("\n".join(registry) + "\n", encoding="ascii")
        print(f"Generated {len(manifest)} IOP modules in {args.output}; allocated through 0x{cursor:08X}")
    except (OSError, ValueError, subprocess.CalledProcessError, StopIteration) as error:
        print(f"recomp_iop: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
