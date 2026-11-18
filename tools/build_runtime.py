"""Relink generated sources and runtime. Use --force after changing headers/compiler flags."""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import build_native as build

r = Path(__file__).resolve().parents[1]
cc = shutil.which("clang++")
c = shutil.which("clang")
if not cc or not c:
    raise SystemExit("clang and clang++ are required")

build.generate_import_fallbacks()
build.generate_grandia_hook_fallbacks()

src = sorted((r / "build/generated/ee").rglob("*.c")) + sorted((r / "build/generated/iop").rglob("*.c"))
src += [p for p in (r / "runtime/src").rglob("*.cpp") if p.name != "gs_iop.cpp"]
src += [
    r / "build/generated/iop/module_registry.cpp",
    r / "build/native/support/iop_import_fallbacks.cpp",
    r / "build/native/support/grandia_hook_fallbacks.cpp",
]

base = [
    "-O2", "-msse4.1", "-mpopcnt", "-fwrapv", "-fno-strict-aliasing", "-ffp-contract=off",
    "-D_WIN32_WINNT=0x0A00", "-DWINVER=0x0A00", "-DNOMINMAX",
    "-Wno-unused-variable", "-Wno-unused-function", "-Wno-unused-but-set-variable",
]
includes = []
for d in ["runtime/include", "build/generated/ee", "build/generated/ee/main", "runtime/src"]:
    includes += ["-I", str(r / d)]

objects = []
force = "--force" in sys.argv
for p in src:
    if not p.exists():
        raise SystemExit(f"Missing source: {p}")
    obj = r / "build/native/obj" / p.relative_to(r).with_suffix(".o")
    if force or not obj.exists() or p.stat().st_mtime > obj.stat().st_mtime:
        obj.parent.mkdir(parents=True, exist_ok=True)
        print("Compile", p.relative_to(r), flush=True)
        if p.suffix == ".c":
            cmd = [c, *base, *includes, "-c", str(p), "-o", str(obj)]
        else:
            cmd = [cc, *base, "-std=c++20", *includes, "-c", str(p), "-o", str(obj)]
        subprocess.run(cmd, check=True)
    if not obj.exists():
        raise SystemExit(f"Missing object: {obj}")
    objects.append(str(obj))

libs = "d3d11 dxgi d3dcompiler xinput1_4 ole32 winmm user32 gdi32 shell32 avrt ksuser uuid comctl32 windowscodecs comdlg32".split()
out = r / "build/native/DigimonWorld4.exe"
out.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([cc, "-static", "-mwindows", "-o", str(out), *objects, *["-l" + x for x in libs]], check=True)
print(f"Built {out}")
