"""Generate C files for the main EE executable in a user's PS2 ISO."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import sys
from collections import Counter

from iso9660 import Iso9660
from inspect_elf import Elf32, PT_LOAD, PF_X

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXECUTABLE_CONFIG = ROOT / "config" / "executables.json"


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


def load_reference_tools() -> tuple[object, object, object]:
    for candidate in _candidate_reference_tools():
        if candidate.is_dir():
            sys.path.insert(0, str(candidate))
            from codeinfo import Module, discover_functions
            from recomp import FuncResolver, Recompiler

            return Module, discover_functions, (FuncResolver, Recompiler)
    searched = ", ".join(str(path) for path in _candidate_reference_tools())
    raise FileNotFoundError(f"Reference tools not found. Searched: {searched}")


def boot_file(image: Iso9660) -> str:
    config = image.read_file(image.find("SYSTEM.CNF")).decode("ascii", errors="replace")
    match = re.search(r"(?im)^\s*BOOT2?\s*=\s*cdrom0:([^;\r\n]+)", config)
    if not match:
        raise ValueError("SYSTEM.CNF does not name a cdrom0 boot executable")
    return match.group(1).strip().replace("\\", "/").strip("/")


def reachable_functions(module: object, functions: list[object], entry: int) -> list[object]:
    by_start = {function.start: function for function in functions}
    pending = [entry]
    reached: set[int] = set()
    while pending:
        address = pending.pop()
        if address in reached:
            continue
        function = by_start.get(address)
        if function is None:
            continue
        reached.add(address)
        for pc in range(function.start, min(function.end, module.code_end), 4):
            instruction = module.insn(pc)
            if instruction.kind not in ("jal", "jump", "branch", "branchl"):
                continue
            target = instruction.target
            if target in by_start and not function.start <= target < function.end:
                pending.append(target)
        for targets in function.switches.values():
            pending.extend(target for target in targets if target in by_start)
    return [by_start[address] for address in sorted(reached)]


def write_registry(
    output: pathlib.Path,
    modules: list[tuple[str, object, list[object], str]],
) -> None:
    declarations = ['#pragma once', '#include "main/ee_decls.h"']
    for name, _, _, prefix in modules[1:]:
        declarations.append(f'#include "{name}/{prefix}_decls.h"')
    (output / "all_decls.h").write_text("\n".join(declarations) + "\n", encoding="ascii")

    registry = ['#include "ee_recomp.h"', '#include "ee_modules.h"', ""]
    for _, _, _, prefix in modules:
        registry.append(f"extern const EEFuncEntry {prefix}_functable[];")
        registry.append(f"extern const unsigned {prefix}_functable_count;")
    registry.extend(("", "const EEModuleInfo g_ee_modules[] = {"))
    for name, module, _, prefix in modules:
        registry.append(
            f'    {{ "{name}", 0x{module.base:08X}u, 0x{module.code_end:08X}u, '
            f"0x{module.data_end:08X}u, 0x{module.bss_end:08X}u, "
            f"{prefix}_functable, &{prefix}_functable_count }},"
        )
    registry.extend(("};", f"const unsigned g_ee_module_count = {len(modules)};",
                     "const EEOverlaySource g_ee_overlay_sources[] = { { 0, 0, 0, 0 } };"))
    (output / "modules.c").write_text("\n".join(registry) + "\n", encoding="ascii")
    files = sorted(path.relative_to(output).as_posix() for path in output.rglob("*.c"))
    (output / "files.txt").write_text("\n".join(files) + "\n", encoding="ascii")
    manifest = [
        {
            "name": name,
            "prefix": prefix,
            "base": module.base,
            "code_end": module.code_end,
            "data_end": module.data_end,
            "bss_end": module.bss_end,
            "nfuncs": len(functions),
        }
        for name, module, functions, prefix in modules
    ]
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "build" / "generated" / "ee")
    parser.add_argument("--code-end", type=lambda value: int(value, 0))
    args = parser.parse_args()

    try:
        Module, discover_functions, recompiler_types = load_reference_tools()
        FuncResolver, Recompiler = recompiler_types
        with Iso9660(args.iso) as image:
            executable_path = boot_file(image)
            executable_name = pathlib.PurePosixPath(executable_path).name
            elf = Elf32(image.read_file(image.find(executable_path)))

        executable_hash = hashlib.sha256(elf.data).hexdigest()
        executable_config = json.loads(EXECUTABLE_CONFIG.read_text(encoding="utf-8"))
        version = executable_config.get(executable_name)
        if version and version["sha256"] != executable_hash and args.code_end is None:
            raise ValueError(
                f"Unexpected {executable_name} SHA-256 {executable_hash}; "
                "pass --code-end only after validating this executable's code boundary"
            )
        if elf.machine != 8:
            raise ValueError(f"Expected a MIPS ELF (machine 8), found machine {elf.machine}")
        if not elf.entry_is_executable():
            raise ValueError("ELF entrypoint is not inside an executable PT_LOAD segment")
        segment = next(
            item for item in elf.segments if item[0] == PT_LOAD and item[6] & PF_X
        )
        base, data, filesz, memsz = segment[2], elf.data[segment[1] : segment[1] + segment[4]], segment[4], segment[5]
        code_end = args.code_end or (
            int(version["code_end"], 0) if version and version["sha256"] == executable_hash else None
        )
        if code_end is None:
            raise ValueError(f"No code boundary known for {executable_name}; pass --code-end")
        if not base <= code_end <= base + filesz or (code_end - base) % 4:
            raise ValueError("--code-end must be aligned and inside the executable segment")

        module = Module("main", base, data, code_end, base + filesz, base + memsz)
        discovered, switches, _, _ = discover_functions(
            module, extra_starts=[elf.entry], data_ptr_scan=True
        )
        reachable = reachable_functions(module, discovered, elf.entry)
        functions = discovered
        embedded_modules = []
        for embedded_config in version.get("embedded_modules", []) if version else []:
            source_address = int(embedded_config["source_address"], 0)
            load_address = int(embedded_config["load_address"], 0)
            embedded_size = int(embedded_config["size"], 0)
            embedded_code_size = int(embedded_config.get("code_size", embedded_config["size"]), 0)
            source_offset = source_address - base
            if (source_offset < 0 or source_offset + embedded_size > len(data)
                    or embedded_code_size > embedded_size or embedded_code_size % 4):
                raise ValueError(f"Embedded module {embedded_config['name']} is outside the ELF segment")
            embedded_image = data[source_offset : source_offset + embedded_size]
            embedded_module = Module(
                embedded_config["name"], load_address, embedded_image,
                load_address + embedded_code_size, load_address + embedded_size, load_address + embedded_size,
            )
            embedded_functions, _, _, _ = discover_functions(
                embedded_module, extra_starts=[load_address], data_ptr_scan=False
            )
            embedded_modules.append((
                embedded_config["name"], embedded_module, embedded_functions,
                f"ee_{embedded_config['name']}",
            ))
        invalid = [
            (current_module.name, pc)
            for _, current_module, current_functions, _ in [("main", module, functions, "ee"), *embedded_modules]
            for function in current_functions
            for pc in range(function.start, min(function.end, current_module.code_end), 4)
            if current_module.insn(pc).kind == "invalid"
        ]
        if invalid:
            sample = ", ".join(f"{name}:{pc:08X}" for name, pc in invalid[:8])
            raise ValueError(f"Reachable functions contain {len(invalid)} invalid instructions: {sample}")

        resolver = FuncResolver()
        resolver.local = {function.start: f"ee_{function.start:08X}" for function in functions}
        for _, _, embedded_functions, _ in embedded_modules:
            resolver.local.update({function.start: f"ee_{function.start:08X}" for function in embedded_functions})
        current_modules = {"main", *(name for name, _, _, _ in embedded_modules)}
        if args.output.is_dir():
            for path in args.output.iterdir():
                generated_module = path.name.startswith(("ee_", "embedded_"))
                if path.is_dir() and generated_module and path.name not in current_modules:
                    shutil.rmtree(path)
        old_manifest = args.output / "manifest.json"
        if old_manifest.is_file():
            for entry in json.loads(old_manifest.read_text(encoding="utf-8")):
                old_name = entry.get("name", "")
                if (old_name not in current_modules and re.fullmatch(r"[A-Za-z0-9_]+", old_name)
                        and (args.output / old_name).is_dir()):
                    shutil.rmtree(args.output / old_name)
        recomp = Recompiler(module, functions, resolver, "ee")
        args.output.mkdir(parents=True, exist_ok=True)
        files = recomp.write(str(args.output / "main"), header_name="ee_recomp.h")
        registry_modules = [("main", module, functions, "ee")]
        for name, embedded_module, embedded_functions, embedded_prefix in embedded_modules:
            embedded_recomp = Recompiler(embedded_module, embedded_functions, resolver, embedded_prefix)
            files.extend(embedded_recomp.write(str(args.output / name), header_name="ee_recomp.h"))
            registry_modules.append((name, embedded_module, embedded_functions, embedded_prefix))
        write_registry(args.output, registry_modules)
        errors = Counter(
            line.strip()
            for path in args.output.rglob("*.c")
            for line in path.read_text(encoding="ascii").splitlines()
            if line.strip().startswith("#error")
        )
        if errors:
            raise ValueError(f"Generated C contains unsupported instructions: {errors.most_common(5)}")
        if recomp.warnings:
            print(f"Warnings: {len(recomp.warnings)}")
            for warning in recomp.warnings[:10]:
                print(f"  {warning}")
        print(
            f"{executable_name}: {len(functions)} discovered functions "
            f"({len(reachable)} directly reachable), "
            f"{len(switches)} switch tables, {sum(len(item[2]) for item in registry_modules)} total functions, "
            f"{len(files)} C files -> {args.output}"
        )
    except (OSError, ValueError, StopIteration) as error:
        print(f"recomp_ee: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
