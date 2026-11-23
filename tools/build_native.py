"""Build the native Windows runtime and locally generated EE/IOP code."""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "runtime"
BUILD = ROOT / "build" / "native"

LIBRARIES = (
    "d3d11", "dxgi", "d3dcompiler", "xinput1_4", "ole32", "winmm", "user32", "gdi32",
    "shell32", "avrt", "ksuser", "uuid", "comctl32", "windowscodecs", "comdlg32",
)


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def generate_import_fallbacks() -> pathlib.Path:
    generated_iop = ROOT / "build" / "generated" / "iop"
    declarations = set()
    for source in generated_iop.rglob("*.c"):
        declarations.update(re.findall(r"\bvoid\s+(iop_imp_[A-Za-z0-9_]+)\s*\(IOPContext\s*\*\s*c\)",
                                       source.read_text(encoding="ascii")))
    implemented = set()
    for source in (RUNTIME / "src").rglob("*.cpp"):
        implemented.update(re.findall(
            r"\bvoid\s+(iop_imp_[A-Za-z0-9_]+)\s*\(IOPContext\s*\*\s*c\)",
            source.read_text(encoding="utf-8"),
        ))
    missing = sorted(declarations - implemented)

    output = BUILD / "support" / "iop_import_fallbacks.cpp"
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = ['#include "common.h"', '#include "iop_recomp.h"', '', 'extern "C" {']
    for name in missing:
        library, index = name.removeprefix("iop_imp_").rsplit("_", 1)
        lines.extend((
            f"void {name}(IOPContext* c) {{",
            '    static unsigned calls = 0;',
            f'    if (calls++ < 3) log_write(LOG_IOP, "Unimplemented IOP import {library}#{index}");',
            "    c->r[2] = 0xFFFFFFFFu;",
            "}",
        ))
    lines.extend(("}", ""))
    output.write_text("\n".join(lines), encoding="ascii")
    print(f"IOP imports: {len(implemented)} implemented, {len(missing)} diagnostic fallbacks")
    return output


def generate_unused_hook_fallbacks() -> pathlib.Path:
    names = (
        "ee_0010C480", "ee_00120F80", "ee_0012E050", "field_002A8400", "ee_0013B450",
        "field_002615F0", "field_002A11F0", "ee_001A65D0", "field_002A0400", "field_0027A270",
        "battle_0032B900", "ee_0019DC90", "ee_00166780", "ee_001DA6A0", "ee_001D5E20",
    )
    output = BUILD / "support" / "unused_hook_fallbacks.cpp"
    lines = [
        '#include "common.h"',
        '#include "ee_recomp.h"',
        "#include <atomic>",
        "",
        'extern "C" {',
    ]
    for name in names:
        lines.extend((
            f"void {name}(EEContext* c) {{",
            "    (void)c;",
            "    static std::atomic_flag logged = ATOMIC_FLAG_INIT;",
            f'    if (!logged.test_and_set()) log_write(LOG_MAIN, "Disabled unused game hook: {name}");',
            "}",
        ))
    lines.extend(("}", ""))
    output.write_text("\n".join(lines), encoding="ascii")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, default=BUILD / "DigimonWorld4.exe")
    args = parser.parse_args()
    iso = args.iso.resolve()

    try:
        run([sys.executable, str(ROOT / "tools" / "recomp_ee.py"), "--iso", str(iso)])
        run([sys.executable, str(ROOT / "tools" / "recomp_iop.py"), "--iso", str(iso)])

        clang = shutil.which("clang")
        clangxx = shutil.which("clang++")
        if not clang or not clangxx:
            raise FileNotFoundError("clang and clang++ must be on PATH")

        BUILD.mkdir(parents=True, exist_ok=True)
        object_root = BUILD / "obj"
        object_root.mkdir(parents=True, exist_ok=True)
        import_fallbacks = generate_import_fallbacks()
        hook_fallbacks = generate_unused_hook_fallbacks()

        c_sources = sorted((ROOT / "build" / "generated" / "ee").rglob("*.c"))
        c_sources += sorted((ROOT / "build" / "generated" / "iop").rglob("*.c"))
        cpp_sources = sorted(RUNTIME.joinpath("src").rglob("*.cpp"))
        cpp_sources = [source for source in cpp_sources if source.name != "gs_iop.cpp"]
        cpp_sources += [ROOT / "build" / "generated" / "iop" / "module_registry.cpp", import_fallbacks,
                hook_fallbacks]

        common = ["-O2", "-msse4.1", "-mpopcnt", "-fwrapv", "-fno-strict-aliasing", "-ffp-contract=off"]
        include_dirs = [RUNTIME / "include", ROOT / "build" / "generated" / "ee",
                        ROOT / "build" / "generated" / "ee" / "main", RUNTIME / "src"]
        include_flags = [flag for directory in include_dirs for flag in ("-I", str(directory))]
        c_flags = common + include_flags
        cpp_flags = common + ["-std=c++20", "-D_WIN32_WINNT=0x0A00", "-DWINVER=0x0A00", "-DNOMINMAX",
                              "-Wno-unused-variable", "-Wno-unused-function", "-Wno-unused-but-set-variable"] + include_flags

        objects = []
        for source in c_sources + cpp_sources:
            relative = source.relative_to(ROOT).with_suffix(".o")
            output = object_root / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            compiler = clang if source.suffix == ".c" else clangxx
            flags = c_flags if source.suffix == ".c" else cpp_flags
            run([compiler, *flags, "-c", str(source), "-o", str(output)])
            objects.append(str(output))

        output = args.out if args.out.is_absolute() else ROOT / args.out
        output.parent.mkdir(parents=True, exist_ok=True)
        libraries = [f"-l{name}" for name in LIBRARIES]
        run([clangxx, "-static", "-mwindows", "-o", str(output), *objects, *libraries])
        print(f"Built {output}")
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"build_native: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())