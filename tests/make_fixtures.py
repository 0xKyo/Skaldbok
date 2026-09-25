"""Regenerates the .zip fixtures of tests/fixtures/ (run by hand when the example pack changes).

  python tests/make_fixtures.py

  frostmarch-tales.zip  the example pack zipped with its folder as the top level (how people usually share one)
  frostmarch-flat.zip   the same, with manifest.json at the top level of the zip
  evil.zip              a pack whose entries try to escape the install folder ("../evil.json")
  no-manifest.zip       a zip that is not a pack
"""
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACK = ROOT / "docs" / "examples" / "frostmarch-tales"
OUT = ROOT / "tests" / "fixtures"
OUT.mkdir(parents=True, exist_ok=True)

with zipfile.ZipFile(OUT / "frostmarch-tales.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(PACK.rglob("*")):
        if f.is_file():
            z.write(f, "frostmarch-tales/" + f.relative_to(PACK).as_posix())

with zipfile.ZipFile(OUT / "frostmarch-flat.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(PACK.rglob("*")):
        if f.is_file():
            z.write(f, f.relative_to(PACK).as_posix())

with zipfile.ZipFile(OUT / "evil.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.write(PACK / "manifest.json", "manifest.json")
    z.writestr("../evil.json", "{}")
    z.writestr("images/../../evil2.json", "{}")

with zipfile.ZipFile(OUT / "no-manifest.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("readme.txt", "not a pack")
    z.writestr("spells.json", '{"spells": []}')

print("wrote", sorted(p.name for p in OUT.iterdir()))
