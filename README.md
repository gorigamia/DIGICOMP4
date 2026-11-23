# DIGICOMP4 — Digimon World 4 Recomp (PS2 USA)

Experimental Windows native recompilation of Digimon World 4, based on the Grandia3-PC runtime and toolchain. Supported game executable: **SLUS_208.36**.

## Download and play

Download **DigimonWorld4-preview-win64.zip** from [Releases](https://github.com/gorigamia/DIGICOMP4/releases), extract into a writable folder, and run **DigimonWorld4.exe**.
Use your own PS2 USA ISO. The executable finds Digimon World 4 (USA).iso or disc1.iso beside it; otherwise it asks for the image once and remembers its location.
The download contains no game image, saves or personal settings. Python and a compiler are not needed to play.

Windows 10/11 x64. GPU mode requires Direct3D 11.3 rasterizer ordered views; software fallback is slower.

## Graphics and controls

- **Selecting 16:9 automatically enables widescreen**, including the game camera and centered HUD. Select 4:3 for the original ratio. F6 switches between these modes.
- F1 opens settings. Keyboard/controller mapping is available from that menu.
- GPU rendering, native internal resolution (1x), FXAA and sharp UI sampling are the default. Higher resolutions cost performance.
- HUD correction: off, smart or strict. Text and panels now use the same correction.
- Experimental camera far clip: 100–200%, default 100%. This does **not** override object LOD, fog or asset streaming, and does not guarantee twice the visible distance.
- Restart after changing camera ratio, far clip, renderer or internal resolution. Old savestates can preserve a previous camera matrix.
- F5/F9 save/load a state; F11 or Alt+Enter toggles fullscreen.
- Default keyboard: arrows move, Enter is Start, S is Cross/confirm. Bindings are configurable.

## Preview status — 2026-11-23

Validated locally: boot/opening video, character selection/creation, and entry into the initial hub with visible ground, platforms and NPCs.
Menu samples were around 59–60 FPS; initial hub samples around 30 FPS. These are observations from one PC, not guarantees.
Full-game scenery traversal, combat, audio accuracy, all aspect ratios and extended distance still need testing. This is a **prerelease**, not a finished port.

Changes:
- Game-side camera correction instead of scaling already-clipped 3D primitives.
- Fixed HUD text left outside panels in widescreen.
- Preserved GS depth; replaced the old depth-scaling option with an experimental camera far clip.
- Removed unsupported Grandia timing and cutscene options.
- Shared-header invalidation prevents incompatible runtime objects being linked together.

## Build

Install Python 3 and LLVM-MinGW (clang and clang++ on PATH), then:

    py tools/build_native.py --iso "E:\Digimon World 4 (USA)\Digimon World 4 (USA).iso"
    py tools/package_release.py

After runtime edits: py tools/build_runtime.py.
Recompiler sources are included in tools/reference. Game-derived generated code stays in ignored build/.
The camera hook verifies its generated instruction signature before applying changes.

Tests:

    py tests/test_camera_patch.py
    clang++ -std=c++20 -O2 -static tests/camera_math.cpp -o camera_math_test.exe
    .\camera_math_test.exe

Additional regression sources: tests/iop_services.cpp, tests/ipu_protocol.cpp, tests/pad_slots.cpp.

## Credits

Grandia3-PC runtime/toolchain by GranasSabre: MIT license retained in LICENSE.
Camera location reference: [ElHecht's PS2 USA widescreen patch](https://github.com/PCSX2/pcsx2_patches/blob/main/patches/SLUS-20836_B871C1D0.pnach). Dynamic aspect integration is implemented in this runtime.
This project is not affiliated with the game's publisher.
