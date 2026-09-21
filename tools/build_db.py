"""Usage: python tools/build_db.py [out.db] [--only rulebook,bestiary,adventure]"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from skaldbok import monsters, rules  # noqa: E402
from skaldbok.build import ROOT, build  # noqa: E402

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = args[0] if args else str(ROOT / "data" / "skaldbok.db")
    only = None
    for a in sys.argv[1:]:
        if a.startswith("--only="):
            only = a.split("=", 1)[1].split(",")
    contexts = build(out, [monsters, rules], only)
    print(f"wrote {out}")
    for ctx in contexts:
        for w in ctx.warnings:
            print("WARNING:", w)
    if not only:                                        # the Core content pack is derived from the finished database
        import export_packs                            # noqa: E402
        export_packs.main(out)
