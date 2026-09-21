"""Extraction of the book's tables (dice tables and plain column tables) from positioned lines."""
from __future__ import annotations

import re
from dataclasses import dataclass, field

from .pdfio import COL_SPLIT, Line, Span, clean, join_lines

DIE_RX = re.compile(r"^D(\d+)$", re.I)
ROLL_RX = re.compile(r"^(\d+)(?:\s*[–\-]\s*(\d+))?$")
ROLL_PREFIX_RX = re.compile(r"^(\d+(?:\s*[–\-]\s*\d+)?)\s+(\S.*)$")


@dataclass
class Row:
    roll_min: int | None
    roll_max: int | None
    roll_text: str | None
    cells: list[str]
    page: int


@dataclass
class Table:
    page: int
    title: str
    dice: str | None
    columns: list[str]
    rows: list[Row] = field(default_factory=list)
    header_line: Line | None = None
    first_roll: int = 0
    last_roll: int = 0
    hdr_y: float = 0.0
    title_override: str | None = None       # set when the title is known better than the header suggests
    problems: list[str] = field(default_factory=list)

    @property
    def x0(self) -> float:
        return self.header_line.x0


def _is_header_font(l: Line) -> bool:
    return l.spans[0].font == "Hideout-Bold" and 7.0 <= l.spans[0].size <= 8.1 and l.y0 < 735


def _header_cells(l: Line) -> list[tuple[float, str]]:
    return [(s.x0, clean(s.text)) for s in l.spans]


def _looks_header(text: str) -> bool:
    letters = [c for c in text if c.isalpha()]
    return bool(letters) and all(c.isupper() for c in letters) and len(text) < 40


def find_headers(lines: list[Line]) -> list[list[tuple[float, str]]]:
    """Header rows of tables on a page: list of [(x0, text), ...] plus anchor line at index -1 (via side list)."""
    return []


@dataclass
class Header:
    y: float
    cells: list[tuple[float, str]]        # (x0, text)
    lines: list[Line]
    ends: list[float] = field(default_factory=list)   # x1 of each cell, parallel to `cells`

    @property
    def x0(self) -> float:
        return self.cells[0][0]


def header_rows(lines: list[Line]) -> list[Header]:
    cands = [l for l in lines if _is_header_font(l) and _looks_header(l.text)]
    cands.sort(key=lambda l: (round(l.y0 / 5), l.x0))
    rows: list[Header] = []
    for l in cands:
        if rows and abs(rows[-1].y - l.y0) < 5:
            rows[-1].lines.append(l)
        else:
            rows.append(Header(l.y0, [], [l]))
    for r in rows:
        r.lines.sort(key=lambda l: l.x0)
        r.cells = [c for l in r.lines for c in _header_cells(l)]
        r.ends = [s.x1 for l in r.lines for s in l.spans]
    return rows


def assign_column(l: Line, colx: list[float], colx1: list[float]) -> int:
    """Column of a cell line. Long text is left-aligned (use its start); short cells are often
    centred under their header (use their centre against the header's extent)."""
    j0 = 0
    for k, cx in enumerate(colx):
        if l.x0 >= cx - 9:
            j0 = k
    if l.width <= 55 and len(colx1) == len(colx):
        mid = (l.x0 + l.x1) / 2
        for k, (a, b) in enumerate(zip(colx, colx1)):
            if a - 6 <= mid <= b + 6:
                return k
    return j0


def _split_blocks(h: Header) -> list[list[tuple[float, str]]]:
    """A header row may hold several side-by-side copies of one table header (D6 NAME | D6 NAME)."""
    blocks: list[list[tuple[float, str]]] = []
    for c in h.cells:
        is_die = bool(DIE_RX.match(c[1]))
        if is_die and blocks and any(DIE_RX.match(x[1]) for x in blocks[-1]):
            blocks.append([c])
        elif is_die or not blocks:
            blocks.append([c])
        else:
            blocks[-1].append(c)
    # split "D6FIRST NAME" spans that were merged by the PDF
    fixed = []
    for b in blocks:
        nb = []
        for x, t in b:
            m = re.match(r"^(D\d+)\s*([A-Z].*)$", t)
            if m and not DIE_RX.match(t):
                nb.append((x, m.group(1)))
                nb.append((x + 20, m.group(2)))
            else:
                nb.append((x, t))
        fixed.append(nb)
    return fixed


def extract_dice_tables(lines: list[Line], headers: list[Header]) -> tuple[list[Table], set[int]]:
    """Tables whose first column is a die roll. Returns tables and ids of consumed lines."""
    tables: list[Table] = []
    used: set[int] = set()
    for h in headers:
        blocks = _split_blocks(h)
        for bi, blk in enumerate(blocks):
            if not DIE_RX.match(blk[0][1]) or len(blk) < 2:
                continue
            die = int(DIE_RX.match(blk[0][1]).group(1))
            roll_x = blk[0][0]
            # horizontal extent of this block
            right = blocks[bi + 1][0][0] - 4 if bi + 1 < len(blocks) else 612
            left = roll_x - 12
            # If there is a left neighbour block, do not leak beyond it
            cols = blk[1:] if len(blk) > 1 else []
            colx = [c[0] for c in cols]
            names = [c[1] for c in cols]
            tbl = Table(page=lines[0].page, title=" ".join(names), dice=f"D{die}", columns=names,
                        header_line=h.lines[0])
            for l in h.lines:
                used.add(id(l))
            # A table that only declares columns in the left half owns lines *starting* there;
            # text starting in the right half belongs to whatever sits next to it (e.g. a statblock).
            if roll_x < COL_SPLIT - 6 and not any(cx >= COL_SPLIT - 6 for cx in colx):
                right = min(right, COL_SPLIT - 6)
            # candidate lines in region below header
            region = [l for l in lines if l.y0 > h.y + 4 and left <= l.x0 < right and id(l) not in used]
            region.sort(key=lambda l: (l.y0, l.x0))
            # roll cells: line at roll column starting with number(s)
            roll_lines: list[tuple[Line, str, str | None]] = []
            for l in region:
                if abs(l.x0 - roll_x) > 14 and not (l.x0 < roll_x + 14 and l.x0 > roll_x - 14):
                    continue
                t = l.text
                m = ROLL_RX.match(t)
                rest = None
                if not m:
                    first = clean(l.spans[0].text)
                    if ROLL_RX.match(first) and len(l.spans) > 1:
                        # roll and cell text are separate spans of one line
                        m = ROLL_RX.match(first)
                        rest = clean("".join(s.text for s in l.spans[1:]))
                        t = first
                    else:
                        m2 = ROLL_PREFIX_RX.match(t)
                        if m2 and ROLL_RX.match(m2.group(1)):
                            m = ROLL_RX.match(m2.group(1))
                            rest = m2.group(2)
                if m:
                    roll_lines.append((l, t if rest is None else m.group(0), rest))
            # keep sequential rolls until die is covered
            picked: list[tuple[Line, str, str | None]] = []
            expect = None
            other_hdr_ys = sorted(o.y for o in headers if o is not h and o.y > h.y
                                  and any(left - 40 <= c[0] < right for c in o.cells))
            y_limit = other_hdr_ys[0] if other_hdr_ys else 1e9
            for l, t, rest in roll_lines:
                if l.y0 >= y_limit:
                    break
                m = ROLL_RX.match(re.sub(r"\s+", "", t)) or ROLL_RX.match(t)
                a = int(m.group(1)); b = int(m.group(2)) if m.group(2) else a
                if expect is None:
                    if a not in (1, ) and not (a > 1 and picked == []):
                        continue
                    expect = a
                if picked and l.y0 - picked[-1][0].y0 > 320:
                    break
                if a == expect and b <= die:
                    picked.append((l, t, rest))
                    expect = b + 1
                    if expect > die:
                        break
            if not picked:
                tbl.problems.append("no roll rows found")
                tables.append(tbl)
                continue
            tbl.first_roll = picked[0][0] and int(ROLL_RX.match(re.sub(r"\s+", "", picked[0][1])).group(1))
            tbl.last_roll = expect - 1
            tbl.hdr_y = h.y
            # row bounds
            ys = [p[0].y0 for p in picked]
            for i, (rl, rt, rest) in enumerate(picked):
                y_top = rl.y0 - 3
                y_bot = ys[i + 1] - 3 if i + 1 < len(ys) else None
                cells = [""] * max(len(cols), 1)
                own: list[Line] = []
                for l in region:
                    if l is rl:
                        continue
                    if l.y0 < y_top:
                        continue
                    if y_bot is not None and l.y0 >= y_bot:
                        continue
                    if y_bot is None:
                        # last row: stop at a vertical gap
                        if own and l.y0 - own[-1].y0 > 22:
                            break
                        if l.font.startswith("MinionPro"):
                            break   # body text below the table
                        if not own and l.y0 - rl.y0 > 22 and abs(l.y0 - rl.y0) > 22:
                            break
                    own.append(l)
                if rest is not None:
                    own.insert(0, rl)
                m = ROLL_RX.match(re.sub(r"\s+", "", rt)) or ROLL_RX.match(rt)
                a = int(m.group(1)); b = int(m.group(2)) if m.group(2) else a
                per_col: list[list[Line]] = [[] for _ in cells]
                for l in own:
                    if id(l) in used:
                        continue
                    if rest is not None and l is rl:
                        # roll and first cell share a line: keep only the cell part
                        if len(l.spans) > 1:
                            cell_spans = l.spans[1:]
                        else:
                            sp = l.spans[0]
                            cell_spans = [Span(rest, sp.font, sp.size, sp.x0 + 12, sp.x1)]
                        l = Line(l.page, cell_spans[0].x0, l.y0, l.x1, l.y1, cell_spans)
                    x = l.x0
                    j = 0
                    for k, cx in enumerate(colx):
                        if x >= cx - 8:
                            j = k
                    per_col[j].append(l)
                    used.add(id(l))
                cells = [join_lines(ls) for ls in per_col]
                used.add(id(rl))
                tbl.rows.append(Row(a, b, m.group(0), cells, rl.page))
            tables.append(tbl)
    return tables, used


def merge_header_lines(headers: list[Header]) -> None:
    """Headers wrapped over two lines ('DURA' / 'BILITY', 'UNIT' / 'OF TIME'): fold the upper part in."""
    folded: list[Header] = []
    for h in headers:
        for up in headers:
            if up is h or not (4 < h.y - up.y < 14):
                continue
            hit = False
            for i, (x, t) in enumerate(list(h.cells)):
                for ux, ut in up.cells:
                    if abs(ux - x) < 6 and not DIE_RX.match(ut) and not DIE_RX.match(t):
                        glue = "" if ut == "DURA" else " "      # "DURA"+"BILITY" is one word
                        h.cells[i] = (x, ut + glue + t)
                        hit = True
            if hit:
                folded.append(up)
    for up in folded:
        if up in headers:
            headers.remove(up)


def grid_rows(lines: list[Line], x0: float, x1: float, y0: float, y1: float) -> list[list[Line]]:
    """Cluster the lines inside a rectangle into rows (same baseline +-4pt), each row sorted by x."""
    sel = sorted((l for l in lines if x0 <= l.x0 < x1 and y0 <= l.y0 < y1), key=lambda l: (l.y0, l.x0))
    rows: list[list[Line]] = []
    for l in sel:
        if rows and abs(rows[-1][0].y0 - l.y0) <= 4:
            rows[-1].append(l)
        else:
            rows.append([l])
    for r in rows:
        r.sort(key=lambda l: l.x0)
    return rows


def extract_derived_rating_tables(lines: list[Line], headers: list[Header], used: set[int]) -> list[Table]:
    """Chapter 2 'Derived Ratings': Movement (+AGL modifiers), Damage Bonus, Base Chance. All centred cells."""
    out: list[Table] = []
    by_name = {c[1]: h for h in headers for c in h.cells}

    def make(title, cols, rows, hdr, dice=None) -> Table:
        t = Table(page=lines[0].page, title=title, dice=dice, columns=cols, header_line=hdr.lines[0])
        t.hdr_y = hdr.y
        t.title_override = title
        for r in rows:
            t.rows.append(Row(None, None, None, [join_lines([c]) for c in r], lines[0].page))
            for c in r:
                used.add(id(c))
        for l in hdr.lines:
            used.add(id(l))
        return t

    if "MOVEMENT" in by_name:
        h = by_name["MOVEMENT"]
        rows = grid_rows(lines, 120, 300, h.y + 8, h.y + 190)
        # kin rows are "Human | 10"; modifier rows are "AGL 1–6 | –4" (attribute and range share a line)
        mod, kin = [], []
        for r in rows:
            (mod if re.match(r"^[A-Z]{3}\s+\d", r[0].text) else kin).append(r)
        out.append(make("Movement", ["KIN", "MOVEMENT"], kin, h))
        if mod:
            out.append(make("Movement modifier", ["ATTRIBUTE / RANGE", "MODIFIER"], mod, h))
    if "DAMAGE BONUS" in by_name:
        h = by_name["DAMAGE BONUS"]
        out.append(make("Damage Bonus", ["STR/AGL", "DAMAGE BONUS"], grid_rows(lines, 370, 520, h.y + 8, h.y + 60), h))
    if "BASE CHANCE" in by_name:
        h = by_name["BASE CHANCE"]
        out.append(make("Base Chance", ["ATTRIBUTE", "BASE CHANCE"], grid_rows(lines, 365, 520, h.y + 8, h.y + 100), h))
    return out


def _split_fused_cells(per_col: list[list[Line]], colx: list[float]) -> None:
    """Two adjacent cells can touch ('6' + '500 gold'). If a column is empty and a word of the
    previous column's text starts right under its header, move that word (and the rest) over."""
    for k in range(1, len(per_col)):
        if per_col[k]:
            continue
        for j in range(k - 1, -1, -1):
            for li, l in enumerate(per_col[j]):
                for si, sp in enumerate(l.spans):
                    cut = sp.split_at_x(colx[k])
                    if not cut:
                        continue
                    a, b = cut
                    head = Line(l.page, l.x0, l.y0, a.x1, l.y1, l.spans[:si] + [a])
                    tail = Line(l.page, b.x0, l.y0, l.x1, l.y1, [b] + l.spans[si + 1:])
                    per_col[j][li] = head
                    per_col[k].append(tail)
                    break
                if per_col[k]:
                    break
            if per_col[k]:
                break


def extract_plain_tables(lines: list[Line], headers: list[Header], used: set[int]) -> list[Table]:
    """Column tables without a die (gear lists, derived-rating tables...). Rows start where column 1 has a line."""
    out: list[Table] = []
    hs = [h for h in headers if not any(DIE_RX.match(c[1]) for c in h.cells)]
    for h in hs:
        if any(id(l) in used for l in h.lines):
            continue
        if not (9 <= lines[0].page <= 126):
            continue        # credits, character sheet, index
        cells = h.cells
        colx = [c[0] for c in cells]
        colx1 = list(h.ends)
        names = [c[1] for c in cells]
        left = colx[0] - 6
        right = min(612.0, colx[-1] + 170)
        # stop at the next header row or heading that overlaps horizontally
        stops = [o.y for o in headers if o.y > h.y + 6 and any(left <= c[0] < right for c in o.cells)]
        y_end = min(stops) if stops else 735.0
        region = [l for l in lines if l.y0 > h.y + 8 and l.y0 < y_end and left <= l.x0 < right
                  and id(l) not in used]
        region.sort(key=lambda l: (l.y0, l.x0))
        # heading lines (bold >= 10) end the table
        cut = None
        for l in region:
            if l.font == "Hideout-Bold" and l.size >= 9.9 and l.text.isupper():
                cut = l.y0
                break
        if cut is not None:
            region = [l for l in region if l.y0 < cut]
        first_col = [l for l in region if abs(l.x0 - colx[0]) <= 6]
        starts: list[Line] = []
        for l in first_col:
            if l.text.startswith("(") and starts:
                continue                # "(Boss)" is the second line of "Knight Champion"
            if not starts or l.y0 - starts[-1].y0 >= 14:
                starts.append(l)
        tbl = Table(page=lines[0].page, title=" ".join(names), dice=None, columns=names,
                    header_line=h.lines[0])
        tbl.hdr_y = h.y
        for l in h.lines:
            used.add(id(l))
        if not starts:
            tbl.problems.append("no rows found")
            out.append(tbl)
            continue
        for i, st in enumerate(starts):
            y_top = st.y0 - 4
            y_bot = starts[i + 1].y0 - 4 if i + 1 < len(starts) else None
            per_col: list[list[Line]] = [[] for _ in names]
            prev_y = st.y0
            for l in region:
                if l.y0 < y_top or (y_bot is not None and l.y0 >= y_bot):
                    continue
                if y_bot is None:
                    if l.y0 - prev_y > 20 or l.font.startswith("MinionPro"):
                        break
                    prev_y = max(prev_y, l.y0)
                per_col[assign_column(l, colx, colx1)].append(l)
                used.add(id(l))
            _split_fused_cells(per_col, colx)
            row_cells = [join_lines(ls) for ls in per_col]
            tbl.rows.append(Row(None, None, None, row_cells, st.page))
            if any(not c for c in row_cells):
                tbl.problems.append(f"row {i} has empty cells: {row_cells}")
        out.append(tbl)
    return out


def merge_split_tables(tables: list[Table]) -> list[Table]:
    """Chain blocks that continue each other (rolls 1-3 | 4-6, or left/right columns) into single tables."""
    done: list[Table] = []
    pool = [t for t in tables if t.rows]
    for t in tables:
        if not t.rows:
            t.problems.append("no roll rows found")
            done.append(t)
    # group by page + columns + dice, then chain by contiguity
    pool.sort(key=lambda t: (t.page, t.first_roll, t.hdr_y, t.header_line.x0))
    used = set()
    for t in pool:
        if id(t) in used:
            continue
        used.add(id(t))
        chain = t
        die = int(t.dice[1:])
        while chain.last_roll < die:
            nxt = [u for u in pool if id(u) not in used and u.page == t.page and u.columns == t.columns
                   and u.dice == t.dice and u.first_roll == chain.last_roll + 1]
            if not nxt:
                break
            nxt.sort(key=lambda u: (abs(u.hdr_y - t.hdr_y), u.header_line.x0))
            u = nxt[0]
            used.add(id(u))
            chain.rows += u.rows
            chain.last_roll = u.last_roll
        if chain.first_roll != 1 or chain.last_roll != die:
            chain.problems.append(f"rolls cover {chain.first_roll}..{chain.last_roll} of {die}")
        done.append(chain)
    done.sort(key=lambda t: (t.page, t.hdr_y, t.header_line.x0))
    return done
