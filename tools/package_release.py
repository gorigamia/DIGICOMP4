"""Package a built preview using a whitelist; never include ISO, saves or local settings."""
from pathlib import Path
import hashlib, zipfile
ROOT = Path(__file__).resolve().parents[1]
def main():
    out=ROOT/"build/release"
    out.mkdir(parents=True,exist_ok=True)
    archive=out/"DigimonWorld4-preview-win64.zip"
    readme=(ROOT/"RELEASE.txt").read_text(encoding="utf-8")
    with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as z:
        z.write(ROOT/"build/native/DigimonWorld4.exe","DigimonWorld4.exe")
        z.write(ROOT/"Grandia3-PC-LICENSE","LICENSE-Grandia3.txt")
        z.writestr("README.txt",readme)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert len(z.namelist()) == 3
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".sha256").write_text(digest+"  "+archive.name)
    print(archive)
    print(digest)
if __name__ == "__main__": main()
