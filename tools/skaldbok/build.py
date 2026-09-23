"""Builds data/skaldbok.db from the rulebook, bestiary and adventure PDFs."""
from __future__ import annotations

import json
import sqlite3
from dataclasses import dataclass, field
from pathlib import Path

from .pdfio import (RULEBOOK_DROP_CAPS, Line, build_vocab, join_lines, norm_key, open_pdf, printed_page_number,
                    resolve_breaks, running_header)
from .sections import Section, anchor_headings, assign_bodies, body_text, build_skeleton, load_pages
from .tables import (Table, extract_derived_rating_tables, extract_dice_tables, extract_plain_tables,
                     header_rows, merge_header_lines, merge_split_tables)

SCHEMA = Path(__file__).with_name("schema.sql")
ROOT = Path(__file__).resolve().parent.parent.parent

SOURCES = [
    dict(key="rulebook", title="Dragonbane Core Rules", file="References/Dragonbane_Rulebook.pdf",
         drop_caps=RULEBOOK_DROP_CAPS, table_pages=(9, 126), page_offset=4),
    dict(key="bestiary", title="Dragonbane Bestiary", file="References/Dragonbane_Bestiary.pdf",
         drop_caps=None, table_pages=(9, 150), page_offset=4),
    dict(key="adventure", title="Dragonbane Adventure: The Misty Vale", file="References/MistyValeAdventure.pdf",
         drop_caps=None, table_pages=(5, 118), page_offset=2),
]


@dataclass
class Context:
    key: str
    source_id: int
    file: str
    doc: object
    pages: dict[int, list[Line]]
    sections: list[Section]
    tables: list[Table] = field(default_factory=list)
    table_section: dict[int, Section] = field(default_factory=dict)   # id(table) -> section
    owner: dict[int, Section] = field(default_factory=dict)           # id(line) -> section
    consumed: set[int] = field(default_factory=set)                   # ids of lines used by structured parsers
    vocab: set[str] = field(default_factory=set)
    warnings: list[str] = field(default_factory=list)
    printed: dict[int, int | None] = field(default_factory=dict)

    def fix(self, text: str) -> str:
        return resolve_breaks(text, self.vocab)

    def warn(self, msg: str) -> None:
        self.warnings.append(f"[{self.key}] {msg}")

    def link(self, page: int) -> str:
        return f"{self.file}#page={page}"

    def sec_id(self, s: Section | int | None) -> int | None:
        if s is None:
            return None
        return self.source_id * 10000 + (s if isinstance(s, int) else s.id)


def line_section_map(sections: list[Section]) -> dict[int, Section]:
    m: dict[int, Section] = {}
    for s in sections:
        if s.anchor is not None:
            m[id(s.anchor)] = s
        if s.anchor_extra is not None:
            m[id(s.anchor_extra)] = s
        for l in s.body_lines:
            m[id(l)] = s
    return m


def prepare(src: dict, source_id: int) -> Context:
    doc = open_pdf(str(ROOT / src["file"]))
    pages = load_pages(doc, src["drop_caps"])
    sections = build_skeleton(doc)
    anchor_headings(doc, sections, pages)
    assign_bodies(sections, pages)
    ctx = Context(src["key"], source_id, src["file"], doc, pages, sections)
    # pages whose number is part of a full-page picture have no text footer: infer it from the book's offset
    ctx.printed = {p: printed_page_number(doc, p) or (p - src["page_offset"] if p > 8 else None) for p in pages}
    ctx.vocab = build_vocab([join_lines(ls) for ls in pages.values()])
    ctx.owner = line_section_map(sections)

    lo, hi = src["table_pages"]
    for pno, lines in pages.items():
        if not lines or not lo <= pno <= hi:
            continue
        hdrs = header_rows(lines)
        merge_header_lines(hdrs)
        found, used = extract_dice_tables(lines, hdrs)
        found = merge_split_tables(found)
        if src["key"] == "rulebook" and pno == 29:
            found += extract_derived_rating_tables(lines, hdrs, used)
        found += extract_plain_tables(lines, hdrs, used)
        ctx.consumed |= used
        for t in found:
            ctx.tables.append(t)
            sec = ctx.owner.get(id(t.header_line))
            if sec is not None:
                ctx.table_section[id(t)] = sec
            for p in t.problems:
                ctx.warn(f"table p{pno} {t.dice} {t.columns}: {p}")
    assign_table_titles(ctx)
    return ctx


_GENERIC_TITLES = {"first name", "nickname", "gear", "attack", "effect", "table", "random events", "inn"}


def assign_table_titles(ctx: Context) -> None:
    """Name tables after the book's own index. The index lists 'Table: X' entries per page in reading order,
    which pairs one-to-one with the tables found on that page (left column first, then top to bottom)."""
    by_page: dict[int, list[Table]] = {}
    for t in ctx.tables:
        if t.title_override is None:
            by_page.setdefault(t.page, []).append(t)
    entries_by_page: dict[int, list[Section]] = {}
    for s in ctx.sections:
        if s.kind == "table":
            entries_by_page.setdefault(s.page_start, []).append(s)
    by_id = {s.id: s for s in ctx.sections}
    for page, tabs in by_page.items():
        entries = entries_by_page.get(page, [])
        if not entries or len(entries) != len(tabs):
            continue
        tabs.sort(key=lambda t: (0 if t.header_line.x0 < 306 else 1, t.header_line.y0))
        for t, e in zip(tabs, entries):
            parent = by_id.get(e.parent_id) if e.parent_id else None
            context = parent.title if parent is not None and parent.level >= 3 and parent.title != "Introduction" else ""
            a, b = norm_key(context), norm_key(e.title)
            if context and e.title.lower() in _GENERIC_TITLES and a not in b:
                t.title_override = f"{context}: {e.title}"
            else:
                t.title_override = e.title


def table_title(ctx: Context, t: Table) -> str:
    if t.title_override:
        return t.title_override
    sec = ctx.table_section.get(id(t))
    cols = " / ".join(c.title() for c in t.columns[:1])
    if sec is None:
        return cols
    if sec.kind == "table":
        return sec.title
    return f"{sec.title}: {cols}" if cols.lower() not in sec.title.lower() else sec.title


def section_body(ctx: Context, s: Section, extra_consumed: set[int] | None = None) -> str:
    skip = ctx.consumed | (extra_consumed or set())
    return ctx.fix(body_text([l for l in s.body_lines if id(l) not in skip]))


def write_core(ctx: Context, con: sqlite3.Connection, monster_lines: set[int]) -> dict[int, int]:
    """Pages, sections, generic tables, FTS. Returns id(table) -> game_tables.id."""
    sid = ctx.source_id
    for p, lines in ctx.pages.items():
        text = ctx.fix(join_lines(list(lines)))
        con.execute("INSERT INTO pages VALUES (?,?,?,?,?)",
                    (sid, p, ctx.printed[p], running_header(ctx.doc, p), text))
    for s in ctx.sections:
        body = section_body(ctx, s)
        con.execute(
            "INSERT INTO sections VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (ctx.sec_id(s), sid, ctx.sec_id(s.parent_id), s.ord, s.level, s.kind, s.title, s.path, s.chapter_no,
             s.page_start, s.page_end, ctx.printed.get(s.page_start), ctx.link(s.page_start),
             1 if s.anchor else 0, body))
        if body:
            con.execute("INSERT INTO search_index(kind,ref_id,title,body,source,page) VALUES (?,?,?,?,?,?)",
                        ("section", ctx.sec_id(s), s.path, body, ctx.key, s.page_start))
    ids: dict[int, int] = {}
    for t in ctx.tables:
        sec = ctx.table_section.get(id(t))
        title = table_title(ctx, t)
        cur = con.execute(
            "INSERT INTO game_tables(source_id,section_id,title,dice,columns,page,printed_page,pdf_link)"
            " VALUES (?,?,?,?,?,?,?,?)",
            (sid, ctx.sec_id(sec), title, t.dice, json.dumps([ctx.fix(c) for c in t.columns]),
             t.page, ctx.printed.get(t.page), ctx.link(t.page)))
        tid = cur.lastrowid
        ids[id(t)] = tid
        texts = []
        for i, r in enumerate(t.rows):
            cells = [ctx.fix(c) for c in r.cells]
            con.execute("INSERT INTO game_table_rows(table_id,ord,roll_min,roll_max,roll_text,cells) VALUES (?,?,?,?,?,?)",
                        (tid, i, r.roll_min, r.roll_max, r.roll_text, json.dumps(cells, ensure_ascii=False)))
            texts.append(f"{r.roll_text or ''} " + " | ".join(cells))
        con.execute("INSERT INTO search_index(kind,ref_id,title,body,source,page) VALUES (?,?,?,?,?,?)",
                    ("table", tid, title, "\n".join(texts), ctx.key, t.page))
    return ids


def build(db_path: str, entity_modules: list | None = None, only: list[str] | None = None) -> list[Context]:
    Path(db_path).parent.mkdir(parents=True, exist_ok=True)
    if Path(db_path).exists():
        Path(db_path).unlink()
    con = sqlite3.connect(db_path)
    con.executescript(SCHEMA.read_text(encoding="utf-8"))
    contexts: list[Context] = []
    for i, src in enumerate(SOURCES, start=1):
        if only and src["key"] not in only:
            continue
        con.execute("INSERT INTO sources VALUES (?,?,?,?,?)",
                    (i, src["key"], src["title"], src["file"], src["page_offset"]))
        ctx = prepare(src, i)
        table_ids = write_core(ctx, con, set())
        ctx.table_db_ids = table_ids  # type: ignore[attr-defined]
        for mod in entity_modules or []:
            mod.run(ctx, con)
        contexts.append(ctx)
    con.execute("INSERT INTO meta VALUES ('schema_version', '2')")
    con.commit()
    con.execute("VACUUM")
    con.close()
    return contexts
