"""Low-level PDF access: turns physical pages into positioned, typed text lines."""
from __future__ import annotations

import re
from dataclasses import dataclass, field

import pymupdf

PAGE_W = 612.28
COL_SPLIT = 306.0
FOOTER_Y = 740.0      # running header (chapter name) and page number live below this
PAGE_OFFSET = 4       # physical page N carries printed page number N-4 (front matter excluded)

SPLIT_GAP = 22.0      # horizontal gap (pt) between spans of one PDF line that marks separate cells
CELL_GAP = 5.5        # gap between characters of small "Hideout" text that separates table cells (a word space is ~2.3)
BREAK = "\x01"        # marks a hard-hyphen line break whose meaning is decided later (see resolve_breaks)
_SOFT_HYPHEN = "­"


@dataclass
class Span:
    text: str
    font: str
    size: float
    x0: float
    x1: float
    xs: list[float] | None = None      # x position of every character of `text` (when known)

    def split_at_x(self, x: float, tol: float = 5.0) -> tuple["Span", "Span"] | None:
        """Cut before the word that starts at x (+-tol). Returns None if no word starts there."""
        if not self.xs:
            return None
        for i in range(1, len(self.text)):
            if self.text[i - 1] == " " and self.text[i] != " " and abs(self.xs[i] - x) <= tol:
                a = Span(self.text[:i], self.font, self.size, self.x0, self.xs[i], self.xs[:i])
                b = Span(self.text[i:], self.font, self.size, self.xs[i], self.x1, self.xs[i:])
                return (a, b) if a.text.strip() and b.text.strip() else None
        return None

    @property
    def bold(self) -> bool:
        return any(k in self.font for k in ("Bold", "Black", "Semibold", "SemiBold"))


@dataclass
class Line:
    page: int                     # physical page, 1-based
    x0: float
    y0: float
    x1: float
    y1: float
    spans: list[Span] = field(default_factory=list)
    order: int = 0                # reading-order rank within the page

    @property
    def text(self) -> str:
        return clean("".join(s.text for s in self.spans))

    @property
    def ends_soft_hyphen(self) -> bool:
        return bool(self.spans) and self.spans[-1].text.rstrip().endswith(_SOFT_HYPHEN)

    @property
    def font(self) -> str:
        return self.spans[0].font if self.spans else ""

    @property
    def size(self) -> float:
        return self.spans[0].size if self.spans else 0.0

    @property
    def width(self) -> float:
        return self.x1 - self.x0

    @property
    def col(self) -> int:
        """0 = left column, 1 = right column (by centre of the line)."""
        return 0 if (self.x0 + self.x1) / 2 < COL_SPLIT else 1

    @property
    def full_width(self) -> bool:
        return self.x0 < 230 and self.x1 > 380

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"<Line p{self.page} ({self.x0:.0f},{self.y0:.0f}) {self.font[:14]}/{self.size} {self.text[:50]!r}>"


def clean(text: str) -> str:
    """Normalise typography without changing meaning."""
    text = text.replace(_SOFT_HYPHEN, "")
    text = re.sub(r"[\x02-\x08\x0e-\x1f]", "", text)        # stray control glyphs (keep \x01 = BREAK marker)
    text = text.replace("‐", "-")
    text = text.replace("\t", " ")
    text = re.sub(r"[  ]+", " ", text)
    return text.strip()


def open_pdf(path: str) -> pymupdf.Document:
    return pymupdf.open(path)


def page_lines(doc: pymupdf.Document, page_no: int, drop_caps: dict[int, str] | None = None) -> list[Line]:
    """All text lines of a physical page (1-based), footer/running-header removed, in reading order."""
    page = doc[page_no - 1]
    lines: list[Line] = []
    for block in page.get_text("rawdict")["blocks"]:
        if block["type"] != 0:
            continue
        for ln in block["lines"]:
            spans = []
            for s in ln["spans"]:
                spans.extend(_split_span(s))
            if not spans:
                continue
            _, y0, _, y1 = ln["bbox"]
            # PyMuPDF may glue text of different columns/cells that share a baseline: split at wide gaps
            group = [spans[0]]
            for sp in spans[1:]:
                both_table = _is_table_font(sp.font, sp.size) and _is_table_font(group[-1].font, group[-1].size)
                if sp.x0 - group[-1].x1 > (CELL_GAP if both_table else SPLIT_GAP):
                    lines.append(Line(page_no, group[0].x0, y0, group[-1].x1, y1, group))
                    group = [sp]
                else:
                    group.append(sp)
            lines.append(Line(page_no, group[0].x0, y0, group[-1].x1, y1, group))
    lines = [l for l in lines if l.y0 < FOOTER_Y]
    if drop_caps and page_no in drop_caps:
        _restore_drop_cap(page, lines, drop_caps[page_no])
    return reading_order(lines)


# Chapter-opening paragraphs start with a decorative *image* letter that has no text layer.
# The letters were read off the rendered pages (see tools/README.md); the restore below only
# fires when the image is really there and the paragraph's first line is indented next to it.
RULEBOOK_DROP_CAPS = {9: "W", 13: "T", 35: "A ", 45: "L", 61: "S", 77: "T", 87: "T", 105: "T", 118: "T"}


def _restore_drop_cap(page, lines: list[Line], letter: str) -> None:
    for info in page.get_image_info():
        x0, y0, x1, y1 = info["bbox"]
        if not (x0 < 70 and 15 < x1 - x0 < 60 and 30 < y1 - y0 < 75):
            continue
        cands = [l for l in lines if l.font.startswith("MinionPro-Regular") and l.size == 10.0
                 and x1 - 8 <= l.x0 <= x1 + 12 and y0 - 6 <= l.y0 <= y0 + 30]
        if cands:
            first = min(cands, key=lambda l: l.y0)
            first.spans[0].text = letter + first.spans[0].text
            return


def _is_table_font(font: str, size: float) -> bool:
    return font.startswith("Hideout") and size <= 9.5


def _split_span(s: dict) -> list[Span]:
    """Rebuild a raw span from its characters, cutting where cells of a table run together.

    PyMuPDF glues characters of one font into a single span even across a wide gap, which turns
    neighbouring table cells into one string ("5 silver" + "Common" -> "5 silverCommon").
    """
    font, size = s["font"], round(s["size"], 1)
    gap = CELL_GAP if _is_table_font(font, size) else 1e9
    out: list[Span] = []
    cur: list[dict] = []

    def flush() -> None:
        if not cur:
            return
        text = "".join(c["c"] for c in cur)
        if text.strip():
            out.append(Span(text, font, size, cur[0]["bbox"][0], cur[-1]["bbox"][2],
                            [c["bbox"][0] for c in cur]))
        cur.clear()

    for ch in s["chars"]:
        if cur and ch["bbox"][0] - cur[-1]["bbox"][2] > gap:
            flush()
        cur.append(ch)
    flush()
    return out


def _footer_lines(doc: pymupdf.Document, page_no: int):
    page = doc[page_no - 1]
    for block in page.get_text("dict")["blocks"]:
        if block["type"] != 0:
            continue
        for ln in block["lines"]:
            if ln["bbox"][1] >= FOOTER_Y:
                yield ln["bbox"][1], clean("".join(s["text"] for s in ln["spans"]))


def printed_page_number(doc: pymupdf.Document, page_no: int) -> int | None:
    for _, t in _footer_lines(doc, page_no):
        if t.isdigit():
            return int(t)
    return None


def running_header(doc: pymupdf.Document, page_no: int) -> str | None:
    for _, t in _footer_lines(doc, page_no):
        if t and not t.isdigit():
            return t
    return None


def reading_order(lines: list[Line]) -> list[Line]:
    """Order lines: bands separated by full-width lines; inside a band left column then right column."""
    def wide(l: Line) -> bool:
        # page-centred headings ("RANGED WEAPONS" above a full-width table) belong to neither column
        centred_title = (l.font == "Hideout-Bold" and l.size >= 9.9 and l.text.isupper()
                         and abs((l.x0 + l.x1) / 2 - COL_SPLIT) < 12 and l.width > 25)
        return l.full_width or l.size >= 20 or centred_title

    seps = sorted(l.y0 for l in lines if wide(l))

    def key(l: Line):
        if wide(l):
            return (sum(1 for y in seps if y < l.y0 - 1) * 2 + 1, 0, l.y0, l.x0)
        return (sum(1 for y in seps if y <= l.y0 + 1) * 2, 1 + l.col, l.y0, l.x0)

    out = sorted(lines, key=key)
    for i, l in enumerate(out):
        l.order = i
    return out


def join_lines(lines: list[Line]) -> str:
    """Join lines of one text flow. Soft-hyphen breaks are healed; hard-hyphen breaks get a BREAK marker."""
    out = ""
    prev: Line | None = None
    for l in lines:
        t = l.text
        if not t:
            continue
        if prev is None or not out:
            out = t
        elif prev.ends_soft_hyphen:
            out += t
        elif out.endswith("-") and out[-2:-1].isalpha() and t[:1].isalpha():
            out += BREAK + t
        else:
            out += " " + t
        prev = l
    return out


_WORD_RX = re.compile(r"[A-Za-z]+")
_BREAK_RX = re.compile(r"([A-Za-z]+)-" + BREAK + r"([A-Za-z]+)")


def build_vocab(texts: list[str]) -> set[str]:
    vocab: set[str] = set()
    for t in texts:
        t = t.replace(BREAK, " ")
        vocab.update(w.lower() for w in _WORD_RX.findall(t))
    return vocab


def resolve_breaks(text: str, vocab: set[str]) -> str:
    """'black-\\x01smith' -> 'blacksmith' when that word exists elsewhere in the book, else 'black-smith'."""
    def fix(m: re.Match) -> str:
        a, b = m.group(1), m.group(2)
        return a + b if (a + b).lower() in vocab else a + "-" + b
    return _BREAK_RX.sub(fix, text).replace(BREAK, "")


def norm_key(text: str) -> str:
    """Case/punctuation-insensitive key for heading matching."""
    t = clean(text).lower()
    t = t.replace("’", "'").replace("‘", "'").replace("“", '"').replace("”", '"')
    t = re.sub(r"[^a-z0-9&/' ]+", " ", t)
    return re.sub(r"\s+", " ", t).strip()
