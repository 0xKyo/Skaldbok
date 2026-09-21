"""Section skeleton from the PDF bookmarks, anchored to heading lines on the pages."""
from __future__ import annotations

import re
from dataclasses import dataclass, field

from .pdfio import Line, clean, join_lines, norm_key, page_lines

PREFIXES = [
    ("sidebar", re.compile(r"^side?bar\s*:?\s*", re.I)),
    ("table", re.compile(r"^table\s*:\s*", re.I)),
    ("optional_rule", re.compile(r"^optional rule\s*:\s*", re.I)),
    ("ability", re.compile(r"^ability\s*:\s*", re.I)),
    ("statblock", re.compile(r"^stat ?blocks?\s*:?\s*", re.I)),
    ("pc_ability", re.compile(r"^pc ability\s*:\s*", re.I)),
    ("npc", re.compile(r"^npcs?\s*:\s*", re.I)),
    ("monster", re.compile(r"^monsters?\s*:\s*", re.I)),
    ("map", re.compile(r"^map\s*:\s*", re.I)),
]


@dataclass
class Section:
    id: int
    parent_id: int | None
    level: int
    kind: str
    title: str              # cleaned title without kind prefix
    raw_title: str          # TOC title as bookmarked
    page_start: int         # physical page
    ord: int = 0
    anchor: Line | None = None        # heading line if located
    anchor_extra: Line | None = None  # second line of a two-line heading
    anchor_rest: Line | None = None   # body text on the same line as a run-in heading
    page_end: int = 0
    chapter_no: int | None = None
    body_lines: list[Line] = field(default_factory=list)
    path: str = ""


def classify(raw: str, level: int) -> tuple[str, str, int | None]:
    t = clean(raw)
    m = re.match(r"^(\d+)\.\s+(.*)$", t)
    if level == 1:
        if m:
            return "chapter", m.group(2), int(m.group(1))
        return "chapter", t, None
    for kind, rx in PREFIXES:
        if rx.match(t):
            rest = rx.sub("", t).strip()
            return kind, rest or t, None
    return "section", t, None


def build_skeleton(doc) -> list[Section]:
    toc = doc.get_toc()
    sections: list[Section] = []
    stack: list[Section] = []
    for i, (level, raw, page) in enumerate(toc):
        kind, title, chap = classify(raw, level)
        while stack and stack[-1].level >= level:
            stack.pop()
        parent = stack[-1] if stack else None
        s = Section(id=i + 1, parent_id=parent.id if parent else None, level=level, kind=kind,
                    title=title, raw_title=clean(raw), page_start=page, ord=i, chapter_no=chap)
        s.path = (parent.path + " > " if parent else "") + title
        sections.append(s)
        stack.append(s)
    return sections


def _residual(l: Line, from_span: int) -> Line | None:
    """The part of a run-in heading line that is body text (everything after the bold label)."""
    rest = l.spans[from_span:]
    if not rest or not "".join(s.text for s in rest).strip():
        return None
    return Line(l.page, rest[0].x0, l.y0, l.x1, l.y1, rest, l.order)


def _heading_candidates(lines: list[Line]) -> list[tuple[Line, str, Line | None, Line | None]]:
    """(line, key, second heading line, body remainder). Whole line or leading bold run-in as heading text.
    A leading bullet (dingbat) before the bold label is skipped."""
    out = []
    prev: Line | None = None
    for l in lines:
        i0 = 1 if len(l.spans) > 1 and l.spans[0].font.startswith("ZapfDingbats") else 0
        first = l.spans[i0]
        if not first.bold or first.font.startswith("MinionPro-SemiboldCapt"):
            prev = None
            continue
        whole = norm_key(clean("".join(s.text for s in l.spans[i0:])))
        out.append((l, whole, None, None))
        rest = _residual(l, i0 + 1)
        lead = norm_key(first.text.rstrip(":. "))
        if lead and lead != whole:
            out.append((l, lead, None, rest))
        # "Strength (STR):" -> "strength"
        nop = norm_key(re.sub(r"\([^)]*\)", "", first.text).rstrip(":. "))
        if nop and nop not in (whole, lead):
            out.append((l, nop, None, rest))
        # headings broken over two lines ("ACTIONS" / "& MOVEMENT")
        if (prev is not None and prev.font == l.font and prev.size == l.size
                and 0 < l.y0 - prev.y0 < 40 and abs(prev.x0 - l.x0) < 120):
            out.append((prev, norm_key(prev.text + " " + l.text), l, None))
        prev = l
    return out


def anchor_headings(doc, sections: list[Section], pages: dict[int, list[Line]]) -> None:
    by_page: dict[int, list[Section]] = {}
    for s in sections:
        by_page.setdefault(s.page_start, []).append(s)
    for pno, secs in by_page.items():
        lines = pages.get(pno)
        if not lines:
            continue
        cands = _heading_candidates(lines)
        used: set[int] = set()
        last_order = -1
        for s in secs:
            keys = {norm_key(s.title), norm_key(s.raw_title)}
            if s.kind == "chapter":
                keys.add(norm_key(re.sub(r"^\d+\.\s*", "", s.raw_title)))
            pick = None
            # a heading that stands alone on its line (box title, section title) beats a run-in label
            for pure in (True, False):
                for prefer_after in (True, False):
                    for l, k, extra, rest in cands:
                        if id(l) in used or k not in keys or (rest is None) != pure:
                            continue
                        if prefer_after and l.order < last_order:
                            continue
                        pick = (l, extra, rest)
                        break
                    if pick:
                        break
                if pick:
                    break
            if pick:
                used.add(id(pick[0]))
                s.anchor = pick[0]
                s.anchor_extra = pick[1]
                s.anchor_rest = pick[2]
                last_order = pick[0].order


_LEADER = re.compile(r"\.{5,}")


def _table_of_contents_lines(pages: dict[int, list[Line]]) -> set[int]:
    """Ids of the lines of the printed table of contents: entries with dot leaders and the "CHAPTER n" / chapter-title
    decoration around them. The contents can spill onto the page of the next chapter (the Rulebook's Preface)."""
    out: set[int] = set()
    for lines in pages.values():
        if sum(1 for l in lines if _LEADER.search(l.text)) < 8:
            continue                                        # not a contents page
        for l in lines:
            bold = l.font.startswith("Hideout-Bold")
            if (_LEADER.search(l.text) or (bold and l.size == 9.0 and re.fullmatch(r"CHAPTER \d+", l.text))
                    or (bold and l.size >= 20 and l.text.isupper())):
                out.add(id(l))
    return out


def assign_bodies(sections: list[Section], pages: dict[int, list[Line]]) -> None:
    """Every line goes to the closest preceding anchored heading (in book reading order)."""
    anchored = [s for s in sections if s.anchor is not None]
    # position key in book reading order
    def pos(s: Section):
        return (s.anchor.page, s.anchor.order)
    anchored.sort(key=pos)
    keys = [pos(s) for s in anchored]
    # for every heading: index of the closest heading at or before it that is not a sidebar
    prev_main: list[int] = []
    last = -1
    for idx, s in enumerate(anchored):
        if s.kind != "sidebar":
            last = idx
        prev_main.append(last)
    heading_ids = {id(s.anchor) for s in anchored} | {id(s.anchor_extra) for s in anchored if s.anchor_extra}
    heading_ids |= _table_of_contents_lines(pages)          # the contents is not text of any section
    import bisect
    for pno in sorted(pages):
        for l in pages[pno]:
            if id(l) in heading_ids:
                continue
            i = bisect.bisect_right(keys, (l.page, l.order)) - 1
            # A sidebar interrupts the main flow. Running text that appears in another column or on another page than
            # the sidebar's own heading is the main flow resuming: the paragraph that carries over into the next
            # column belongs to the section that was open before the box, not to the box.
            if i >= 0 and anchored[i].kind == "sidebar" and l.font.startswith("MinionPro"):
                a = anchored[i].anchor
                inside = l.page == a.page and l.col == a.col and l.y0 >= a.y0 - 2
                if not inside:
                    i = prev_main[i]
            if i >= 0:
                anchored[i].body_lines.append(l)
    # text that shares a line with a run-in heading ("Mages: If you play a mage...") is body text
    for s in anchored:
        if s.anchor_rest is not None:
            s.body_lines.insert(0, s.anchor_rest)
    # page_end = last page having body lines (or its own page)
    for s in sections:
        pgs = [l.page for l in s.body_lines] + [s.page_start]
        s.page_end = max(pgs)


def body_text(lines: list[Line]) -> str:
    """Join lines into paragraphs; repair hyphenation at line ends."""
    paragraphs: list[list[Line]] = []
    prev: Line | None = None
    for l in lines:
        if not l.text:
            continue
        if prev is None:
            new_par = True
        else:
            same_flow = prev.page == l.page and prev.col == l.col
            gap = l.y0 - prev.y0 if same_flow else 99
            if (not same_flow and l.text[:1].islower() and not prev.text.endswith((".", "!", "?", ":", ";", "”", '"', "’", ")"))
                    and prev.font.startswith("MinionPro") and l.font.startswith("MinionPro")):
                gap = 12        # a sentence carried over to the next column or page is still the same paragraph
            bullet = l.spans[0].font.startswith("ZapfDingbats")
            first = paragraphs[-1][0]
            # a bullet paragraph has indented continuation lines; a line back at the bullet's margin is new text
            back_at_margin = (first.spans[0].font.startswith("ZapfDingbats") and not bullet
                              and same_flow and l.x0 <= first.x0 + 4)
            new_par = (gap > 16 or gap < 0 or bullet or back_at_margin
                       or (l.spans[0].bold and not prev.spans[0].bold and l.size > 9.5))
        if new_par:
            paragraphs.append([l])
        else:
            paragraphs[-1].append(l)
        prev = l
    return "\n".join(join_lines(p) for p in paragraphs).strip()


def load_pages(doc, drop_caps: dict[int, str] | None = None) -> dict[int, list[Line]]:
    return {p: page_lines(doc, p, drop_caps) for p in range(1, len(doc) + 1)}

