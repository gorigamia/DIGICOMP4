"""Relink generated sources and runtime. Use --force after changing headers/compiler flags."""
from pathlib import Path
import shutil, subprocess, sys
import build_native as build
r = Path(__file__).resolve().parents[1]
cc = shutil.which('clang++')
if not cc: raise SystemExit('clang++ is required')
build.generate_import_fallbacks()
from patch_dw4_camera import patch
patch(r/"build/generated/ee")
src = sorted((r/'build/generated/ee').rglob('*.c')) + sorted((r/'build/generated/iop').rglob('*.c'))
src += [p for p in (r/'runtime/src').rglob('*.cpp') if p.name != 'gs_iop.cpp']
src += [r/'build/generated/iop/module_registry.cpp',r/'build/native/support/iop_import_fallbacks.cpp',r/'build/native/support/unused_hook_fallbacks.cpp']
flags = ['-O2','-msse4.1','-mpopcnt','-fwrapv','-fno-strict-aliasing','-ffp-contract=off','-std=c++20','-D_WIN32_WINNT=0x0A00','-DWINVER=0x0A00','-DNOMINMAX','-Wno-unused-variable','-Wno-unused-function','-Wno-unused-but-set-variable']
for d in ['runtime/include','build/generated/ee','build/generated/ee/main','runtime/src']: flags += ['-I',str(r/d)]
# Shared headers can change layouts without touching the corresponding .cpp.
# Invalidate all objects conservatively rather than link incompatible layouts.
headers = list((r/'runtime').rglob('*.h')) + list((r/'build/generated').rglob('*.h'))
header_mtime = max((p.stat().st_mtime for p in headers), default=0)
c_headers = list((r/'runtime/include').rglob('*.h')) + list((r/'build/generated').rglob('*.h'))
c_header_mtime = max((p.stat().st_mtime for p in c_headers), default=0)
objects=[]
for p in src:
    obj=r/'build/native/obj'/p.relative_to(r).with_suffix('.o')
    if '--force' in sys.argv or not obj.exists() or max(p.stat().st_mtime, header_mtime if p.suffix == '.cpp' else c_header_mtime)>obj.stat().st_mtime:
        obj.parent.mkdir(parents=True, exist_ok=True)
        print('Compile',p.relative_to(r),flush=True)
        compile_flags = flags if p.suffix == '.cpp' else [x for x in flags if x != '-std=c++20']
        compiler = cc if p.suffix == '.cpp' else shutil.which('clang')
        subprocess.run([compiler,*compile_flags,'-c',str(p),'-o',str(obj)],check=True)
    if not obj.exists(): raise SystemExit('Missing object; run build_native.py first: '+str(obj))
    objects.append(str(obj))
libs='d3d11 dxgi d3dcompiler xinput1_4 ole32 winmm user32 gdi32 shell32 avrt ksuser uuid comctl32 windowscodecs comdlg32'.split()
subprocess.run([cc,'-static','-mwindows','-o',str(r/'build/native/DigimonWorld4.exe'),*objects,*['-l'+x for x in libs]],check=True)
print('Built DigimonWorld4.exe')
