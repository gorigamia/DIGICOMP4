# Digimon World 4 Recomp (PS2 USA)

Experimental native recompilation/reimplementation effort for **Digimon World 4 (USA)** (`SLUS_208.36`), built around the Grandia 3 PC runtime/C-backend execution model.

> Status: development preview. Boot and early opening/logo/movie progress have been observed in prior local validation, but menu/gameplay/sound/scenery behavior is not fully validated. Expect bugs and incomplete behavior.

## What this repository contains

- `config/` — executable metadata (hash, code boundary, embedded modules)
- `tools/` — ISO inspection and EE/IOP code generation/build/packaging scripts
- `tools/reference/` — vendored reference recompiler components used by generation
- `runtime/` — native Windows runtime (platform, GS/VU/IOP/CDVD/audio/etc.)
- `tests/` — focused protocol/service tests
- `Grandia3-PC-LICENSE` — upstream MIT attribution for Grandia3-PC-derived components

Game data is **not** included. You must supply your own legally obtained disc image.

## Requirements

- Windows 10/11 x64
- Your own `Digimon World 4 (USA).iso` (validated executable: `SLUS_208.36`)
- For building from source: Python 3 and LLVM-MinGW/Clang (`clang`, `clang++`) on `PATH`
- Direct3D 11-capable system for presentation

## Run a built preview

Place `Digimon World 4 (USA).iso` next to `DigimonWorld4.exe` (or keep a valid configured path) and launch the EXE.  
Runtime config/log are written beside the executable as `digimonworld4.ini` / `digimonworld4.log` (in a writable folder).

Default profile is conservative/experimental (software-oriented defaults, reduced speed in some setups). See CLI flags in `runtime/src/main.cpp` for diagnostics/test options.

## Build from source

From the repository root:

```powershell
py tools\build_native.py --iso "E:\path\to\Digimon World 4 (USA).iso"
```

This generates EE/IOP sources under `build/generated/`, compiles runtime + generated code, and produces `build/native/DigimonWorld4.exe`.

After changing headers/compiler flags or for relinking generated/runtime objects:

```powershell
py tools\build_runtime.py --force
```

Package a preview zip (whitelisted files only):

```powershell
py tools\package_release.py
```

Artifacts go to `build/release/` (EXE + license + release notes, with SHA256).

## Notes

- The ISO is read in place; it is never copied into the repo or modified.
- Do not commit copyrighted game data, ISOs, saves, logs, screenshots, or local build outputs.
- For executables other than the validated `SLUS_208.36`, a new code boundary must be validated and passed via `--code-end`.

## License / attribution

This repository carries its own `LICENSE` and retains `Grandia3-PC-LICENSE` for upstream Grandia3-PC-derived runtime/tooling components used by this project. Keep both when distributing source or binaries as applicable.
