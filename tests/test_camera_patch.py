import importlib.util
from pathlib import Path
import tempfile
import unittest
spec = importlib.util.spec_from_file_location("camera_patch", Path(__file__).parents[1]/"tools/patch_dw4_camera.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
class CameraPatch(unittest.TestCase):
    def test_signature_and_idempotence(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); (root/"main").mkdir()
            p=root/"main/ee_033.c"
            original=('#include "all_decls.h"\n'
                '    // 001B6DE8: mov.s $f12, $f23\n'
                '    c->fpr[12].u = c->fpr[23].u;\n'
                '    // 001B6DEC: neg.s $f13, $f23\n'
                '    c->fpr[13].u = c->fpr[23].u ^ 0x80000000u;\n'
                '    // 001B6DF0: jal 0x001b6cd0\n')
            p.write_text(original)
            mod.patch(root); once=p.read_text()
            self.assertEqual(once.count("DW4_CAMERA_FRUSTUM"),1)
            mod.patch(root); self.assertEqual(p.read_text(),once)
            p.write_text(original.replace("fpr[23].u;", "fpr[22].u;"))
            with self.assertRaises(ValueError):mod.patch(root)
            self.assertNotIn("DW4_CAMERA_FRUSTUM",p.read_text())
if __name__ == "__main__": unittest.main()
