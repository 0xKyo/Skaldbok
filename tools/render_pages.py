"""Optional: pre-render the pages of the three PDFs so the app can show the original inside its own window.

    python tools/render_pages.py            # ~100 dpi JPEG, all books
    python tools/render_pages.py --dpi 130 --quality 75 --only bestiary

Output: data/pages/<book>/<physical page>.jpg  (a few tens of MB; delete the folder any time)
The app uses these images when they exist and falls back to opening the PDF otherwise.
"""
import argparse
import sys
from pathlib import Path

import pymupdf

ROOT = Path(__file__).resolve().parent.parent
BOOKS = {
    "rulebook": "References/Dragonbane_Rulebook.pdf",
    "bestiary": "References/Dragonbane_Bestiary.pdf",
    "adventure": "References/Adventure.pdf",
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dpi", type=int, default=100)
    ap.add_argument("--quality", type=int, default=70)
    ap.add_argument("--only", choices=BOOKS.keys())
    args = ap.parse_args()

    total = 0
    for key, rel in BOOKS.items():
        if args.only and key != args.only:
            continue
        doc = pymupdf.open(ROOT / rel)
        out = ROOT / "data" / "pages" / key
        out.mkdir(parents=True, exist_ok=True)
        for i in range(len(doc)):
            pix = doc[i].get_pixmap(dpi=args.dpi, alpha=False)
            path = out / f"{i + 1}.jpg"
            pix.save(str(path), jpg_quality=args.quality)
            total += path.stat().st_size
        print(f"{key}: {len(doc)} pages")
    print(f"total {total / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
