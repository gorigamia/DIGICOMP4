"""Install a guarded camera hook into generated SLUS_208.36 code."""
from pathlib import Path
def patch(output):
    marker = "    dw4_camera_frustum(c); // DW4_CAMERA_FRUSTUM"
    anchor = "    // 001B6DF0: jal 0x001b6cd0"
    candidates = [p for p in (Path(output)/"main").glob("ee_*.c")
                  if anchor in p.read_text(encoding="ascii")]
    if len(candidates) != 1:
        raise ValueError("DW4 camera hook: expected one verified call site")
    p = candidates[0]
    s = p.read_text(encoding="ascii")
    if marker in s: return
    expected = ("    // 001B6DE8: mov.s $f12, $f23\n"
                "    c->fpr[12].u = c->fpr[23].u;\n"
                "    // 001B6DEC: neg.s $f13, $f23\n"
                "    c->fpr[13].u = c->fpr[23].u ^ 0x80000000u;\n")
    if expected + anchor not in s:
        raise ValueError("DW4 camera hook: instruction signature changed")
    s = s.replace(anchor, marker+"\n"+anchor, 1)
    s = s.replace('#include "all_decls.h"', '#include "all_decls.h"\nextern void dw4_camera_frustum(EEContext* c);', 1)
    p.write_text(s,encoding="ascii")
if __name__ == "__main__":
    patch(Path(__file__).resolve().parents[1]/"build/generated/ee")
