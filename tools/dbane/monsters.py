"""Monsters, creatures and NPCs of the three books: stat blocks, attack tables, abilities, art."""
from __future__ import annotations

import json
import re
import sqlite3
from pathlib import Path

import pymupdf

from .build import ROOT, Context
from .pdfio import Line, Span, clean, join_lines, norm_key
from .sections import Section, body_text

IMAGE_DIR = ROOT / "data" / "packs" / "core" / "images"      # the Core pack owns its creature art
STAT_LABELS = {
    "ferocity", "size", "movement", "mov", "armor", "typical armor", "hp", "wp", "skills", "skill",
    "typical weapon", "typical weapons", "weapons", "weapon", "damage bonus", "damage bonus str",
    "damage bonus agl", "heroic abilities", "typical gear", "gear",
}
NON_VARIANT_TITLES = {"MONSTER ATTACKS", "RANDOM ENCOUNTER", "ADVENTURE SEED", "ABILITIES"}
ATTACK_NAME_RX = re.compile(r"^(.{2,80}?[!?])\s+(\S.*)$", re.S)


def norm_label(label: str) -> str:
    return re.sub(r"\s+", " ", label.lower().replace(".", "").replace(":", "")).strip()


# --------------------------------------------------------------------------------------------
# which sections are monsters


def descendants(sections: list[Section], root: Section) -> list[Section]:
    kids: dict[int, list[Section]] = {}
    for s in sections:
        if s.parent_id is not None:
            kids.setdefault(s.parent_id, []).append(s)
    out: list[Section] = []
    stack = [root]
    while stack:
        cur = stack.pop()
        for k in kids.get(cur.id, []):
            out.append(k)
            stack.append(k)
    return out


def ancestor_chapter(sections: list[Section], s: Section) -> Section | None:
    by_id = {x.id: x for x in sections}
    cur = s
    while cur.parent_id is not None:
        cur = by_id[cur.parent_id]
    return cur if cur.level == 1 else None


def find_monsters(ctx: Context) -> list[tuple[Section, str, str | None]]:
    """(section, kind, category)"""
    out = []
    secs = ctx.sections
    for s in secs:
        chap = ancestor_chapter(secs, s)
        category = chap.title if chap else None
        if ctx.key == "bestiary":
            if s.level == 2 and chap is not None and chap.chapter_no and 1 <= chap.chapter_no <= 9:
                out.append((s, "monster", category))
        elif ctx.key == "rulebook" and chap is not None and chap.chapter_no == 7 and s.level == 2 \
                and s.title not in ("Introduction", "Common Animals"):      # animals are rows of a table, see below
            out.append((s, "monster", category))
        if ctx.key in ("adventure", "rulebook") and s.kind in ("monster", "npc"):
            out.append((s, s.kind, category))
    if ctx.key == "rulebook":
        # spells that summon a creature (Gnome, Salamander, Sylph, Undine) carry a "Statblock" entry in the index
        by_id = {x.id: x for x in secs}
        have = {s.id for s, _k, _c in out}
        for s in secs:
            chap = ancestor_chapter(secs, s)
            if s.kind == "statblock" and chap is not None and chap.chapter_no == 5 and s.parent_id in by_id:
                parent = by_id[s.parent_id]
                if parent.id not in have:
                    have.add(parent.id)
                    out.append((parent, "monster", "Summoned creatures (Elementalism)"))
    return out


# --------------------------------------------------------------------------------------------
# stat blocks


def _is_box_line(l: Line) -> bool:
    return l.font.startswith("Hideout") and l.size <= 10.0


def _is_title(l: Line) -> bool:
    if l.text in NON_VARIANT_TITLES:        # banners ("MONSTER ATTACKS") come in 9pt too; they end a box
        return True
    return l.font == "Hideout-Bold" and l.size >= 9.9 and l.text.isupper() and len(l.text) < 40


def _strip_first_span(l: Line, n: int = 1) -> Line | None:
    rest = l.spans[n:]
    if not rest:
        return None
    return Line(l.page, rest[0].x0, l.y0, l.x1, l.y1, rest)


def _join_split_labels(l: Line) -> Line:
    """Some labels are typeset as the word in one span and a bold ':' in the next ('HP' + ': 5 per power level').
    Re-glue them into one bold 'HP:' span so the label is recognised."""
    if not any(clean(sp.text) == ":" or clean(sp.text).startswith(":") and sp.bold for sp in l.spans[1:]):
        return l
    spans: list[Span] = []
    for sp in l.spans:
        colon_only = sp.bold and clean(sp.text).startswith(":") and spans
        if not colon_only:
            spans.append(sp)
            continue
        prev = spans[-1]
        head, _, word = prev.text.rstrip().rpartition(" ")
        if not word:
            spans.append(sp)
            continue
        rest = sp.text.lstrip()[1:]
        if head.strip():
            spans[-1] = Span(head + " ", prev.font, prev.size, prev.x0, prev.x1, None)
        else:
            spans.pop()
        spans.append(Span(word + ":", sp.font, sp.size, prev.x0, sp.x0 + 4, None))
        if rest.strip():
            spans.append(Span(rest, prev.font, prev.size, sp.x0 + 4, sp.x1, None))
    return Line(l.page, spans[0].x0, l.y0, spans[-1].x1, l.y1, spans, l.order)


def parse_fields(lines: list[Line]) -> tuple[list[Line], list[tuple[str, list[Line]]]]:
    """Split a box into (prefix lines, [(label, value lines)]). A label is a bold span ending in ':'."""
    prefix: list[Line] = []
    fields: list[tuple[str, list[Line]]] = []
    for l in lines:
        l = _join_split_labels(l)
        # a line may hold several labels ("Movement: 8  HP: 5 per power level"): cut at every bold "Label:" span
        cuts = [i for i, sp in enumerate(l.spans) if sp.bold and clean(sp.text).endswith(":")]
        if not cuts:
            (fields[-1][1] if fields else prefix).append(l)
            continue
        if cuts[0] > 0:                        # text before the first label continues the previous field
            head = Line(l.page, l.spans[0].x0, l.y0, l.spans[cuts[0] - 1].x1, l.y1, l.spans[:cuts[0]], l.order)
            (fields[-1][1] if fields else prefix).append(head)
        for n, i in enumerate(cuts):
            end = cuts[n + 1] if n + 1 < len(cuts) else len(l.spans)
            body = l.spans[i + 1:end]
            piece = Line(l.page, body[0].x0, l.y0, body[-1].x1, l.y1, body, l.order) if body else None
            fields.append((clean(l.spans[i].text)[:-1].strip(), [piece] if piece else []))
    return prefix, fields


def group_box_lines(lines: list[Line]) -> list[list[Line]]:
    groups: list[list[Line]] = []
    prev: Line | None = None
    for l in lines:
        if not _is_box_line(l) or _is_title(l):
            prev = None
            continue
        near = prev is not None and prev.page == l.page and (l.y0 - prev.y0 <= 22) and l.y0 - prev.y0 >= -3
        if near:
            groups[-1].append(l)
        else:
            groups.append([l])
        prev = l
    return groups


def is_statblock(fields: list[tuple[str, list[Line]]]) -> bool:
    return any(norm_label(lb) in STAT_LABELS for lb, _ in fields[:3])


# --------------------------------------------------------------------------------------------
# images


def extract_art(ctx: Context, page_from: int, page_to: int, dest: Path) -> tuple[str, int] | None:
    """Largest illustration on the pages (not the parchment background). Returns (path, page)."""
    best = None
    furniture = reused_xrefs(ctx)
    for pno in range(page_from, page_to + 1):
        page = ctx.doc[pno - 1]
        for im in page.get_images(full=True):
            xref, smask = im[0], im[1]
            if xref in furniture:
                continue
            try:
                bb = page.get_image_bbox(im)
            except Exception:
                continue
            w, h = bb.width, bb.height
            if w < 200 or h < 200:
                continue
            if w >= 610 and h >= 780 and smask:     # full-page parchment/vignette overlays carry an alpha mask
                continue
            area = w * h
            if best is None or area > best[0]:
                best = (area, xref, smask, pno)
    if best is None:
        return None
    _, xref, smask, pno = best
    return _save_image(ctx, xref, smask, dest), pno


_REUSED: dict[int, set[int]] = {}


def reused_xrefs(ctx: Context) -> set[int]:
    """Images drawn on more than two pages are page furniture (parchment boxes, banners), not art."""
    key = id(ctx.doc)
    if key not in _REUSED:
        count: dict[int, int] = {}
        for pno in range(len(ctx.doc)):
            for im in {i[0] for i in ctx.doc[pno].get_images(full=True)}:
                count[im] = count.get(im, 0) + 1
        _REUSED[key] = {x for x, n in count.items() if n > 2}
    return _REUSED[key]


def extract_portrait(ctx: Context, anchor: Line, dest: Path) -> tuple[str, int] | None:
    """Adventure creatures: a portrait sits right above the box whose title is `anchor`."""
    page = ctx.doc[anchor.page - 1]
    cx = (anchor.x0 + anchor.x1) / 2
    best = None
    furniture = reused_xrefs(ctx)
    for im in page.get_images(full=True):
        if im[0] in furniture:
            continue
        try:
            bb = page.get_image_bbox(im)
        except Exception:
            continue
        w, h = bb.width, bb.height
        if not (110 <= w <= 400 and h >= 90) or not 0.5 <= w / h <= 1.9:
            continue        # wide strips are parchment banners, not portraits
        if abs((bb.x0 + bb.x1) / 2 - cx) > 150:
            continue
        if not (anchor.y0 - 70 <= bb.y1 <= anchor.y0 + 45):
            continue
        if best is None or w * h > best[0]:
            best = (w * h, im)
    if best is None:
        return None
    return _save_image(ctx, best[1][0], best[1][1], dest), anchor.page


def _save_image(ctx: Context, xref: int, smask: int, dest: Path) -> str:
    pix = pymupdf.Pixmap(ctx.doc, xref)
    if pix.alpha == 0 and smask:
        pix = pymupdf.Pixmap(pix, pymupdf.Pixmap(ctx.doc, smask))
    if pix.n - pix.alpha > 3:                   # CMYK etc.
        pix = pymupdf.Pixmap(pymupdf.csRGB, pix)
    while pix.width > 900:
        pix.shrink(1)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if pix.alpha:
        path = dest.with_suffix(".png")
        pix.save(str(path))
    else:
        path = dest.with_suffix(".jpg")
        pix.save(str(path), jpg_quality=82)
    return str(path.relative_to(ROOT)).replace("\\", "/")


BOOK_REF_RX = re.compile(r"[^.]*\b(?:Rulebook|Bestiary)\b[^.]*\.", re.I)
STATS_REF_RX = re.compile(r"[^.]*\bstat(?:s|\s?blocks?)?\b[^.]*\b(?:Rulebook|Bestiary|page\s+\d+)[^.]*\.", re.I)


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


# --------------------------------------------------------------------------------------------


def run(ctx: Context, con: sqlite3.Connection) -> None:
    found = find_monsters(ctx)
    if not found:
        return
    sid = ctx.source_id
    secs = ctx.sections
    monster_ids: dict[int, int] = {}          # section.id -> monsters.id
    expect: dict[int, tuple[Section, int, bool]] = {}   # monsters.id -> (section, #stat blocks, has attacks) per the TOC
    order = sorted(secs, key=lambda s: s.ord)
    ord_index = {s.id: i for i, s in enumerate(order)}
    stats = {"monsters": 0, "statblocks": 0, "attacks": 0, "abilities": 0, "images": 0}

    # Crowded pages (adventure): a box belongs to the nearest creature title above it, by geometry.
    def geo_choose(cands: list[Section], x_center: float, y: float) -> Section | None:
        above = [s for s in cands if s.anchor.y0 - 5 <= y] or cands
        if not above:
            return None
        return min(above, key=lambda s: abs(y - s.anchor.y0) + 1.5 * abs(x_center - (s.anchor.x0 + s.anchor.x1) / 2))

    summon_ids = {s.id for s, _k, c in found if c and c.startswith("Summoned")}

    def uses_geometry(s: Section) -> bool:
        return (ctx.key == "adventure" or (ctx.key == "rulebook" and s.kind in ("monster", "npc"))
                or s.id in summon_ids)

    anchors_by_page: dict[int, list[Section]] = {}
    for s, _k, _c in found:
        if s.anchor is not None and uses_geometry(s):
            anchors_by_page.setdefault(s.anchor.page, []).append(s)
    geo_blocks: dict[int, list] = {}
    for page, cands in anchors_by_page.items():
        page_lines_ = [l for l in ctx.pages[page] if id(l) not in ctx.consumed]
        box_titles = [l for l in page_lines_ if _is_title(l) and l.text not in NON_VARIANT_TITLES]
        for g in group_box_lines(page_lines_):
            prefix, fields = parse_fields(g)
            if fields and is_statblock(fields):
                cx = (min(x.x0 for x in g) + max(x.x1 for x in g)) / 2
                best = None
                # a box titled with a creature's own name belongs to that creature ("GNOME" above the gnome's box)
                above = [t for t in box_titles if 0 < g[0].y0 - t.y0 < 70 and abs((t.x0 + t.x1) / 2 - cx) < 110]
                if above:
                    title = norm_key(max(above, key=lambda t: t.y0).text)
                    best = next((s for s in cands if norm_key(s.title) == title), None)
                if best is None:
                    best = geo_choose(cands, cx, g[0].y0)
                if best is not None:
                    geo_blocks.setdefault(best.id, []).append((g, prefix, fields))

    for sec, kind, category in found:
        subtree = descendants(secs, sec)
        sub_titles = {d.id: norm_label(d.title) for d in subtree}
        enc = next((d for d in subtree if norm_label(d.title) == "random encounter"), None)
        seed = next((d for d in subtree if norm_label(d.title) == "adventure seed"), None)
        excluded_ids = {d.id for d in (enc, seed) if d}

        own_lines: list[Line] = list(sec.body_lines)
        stat_owner: dict[int, Section] = {id(l): sec for l in own_lines}
        for d in subtree:
            if d.id in excluded_ids or d.kind in ("pc_ability", "table"):
                continue
            for l in d.body_lines:
                stat_owner[id(l)] = d
            own_lines += d.body_lines
        own_lines = [l for l in own_lines if id(l) not in ctx.consumed]
        own_lines.sort(key=lambda l: (l.page, l.order))

        # Boxes (stat blocks, ability sidebars) are free-standing objects: by reading order they can fall under
        # the *next* heading (right column after "Adventure Seed"). Books with one creature per page spread are
        # therefore scanned over the creature's pages; crowded pages (adventure) fall back to heading ownership.
        i0 = ord_index[sec.id]
        nxt = next((s for s in order[i0 + 1:] if s.level <= sec.level), None)
        shares_page = nxt is not None and nxt.page_start <= sec.page_start
        last_page = min((nxt.page_start - 1) if nxt else sec.page_start + 1, sec.page_start + 2)
        last_page = max(last_page, sec.page_start)
        if ctx.key == "adventure" or shares_page:
            pool = own_lines
        else:
            pool = sorted((l for p in range(sec.page_start, last_page + 1) for l in ctx.pages.get(p, [])
                           if id(l) not in ctx.consumed), key=lambda l: (l.page, l.order))

        # description and quote: running text of the monster's own pages
        body = sorted(sec.body_lines, key=lambda l: (l.page, l.order))
        if category and category.startswith("Summoned"):
            # a spell entry opens with its bullet fields (Rank, Requirement...) whose last line may wrap; the
            # creature's description is what follows them
            bullets = [i for i, l in enumerate(body) if l.spans[0].font.startswith("ZapfDingbats")]
            if bullets:
                i = bullets[-1] + 1
                while i < len(body) and body[i].x0 - body[bullets[-1]].x0 > 6:
                    i += 1
                body = body[i:]
        text_lines = [l for l in body if l.font.startswith("MinionPro") and id(l) not in ctx.consumed]
        quote_lines = [l for l in text_lines if "SemiboldIt" in l.font and l.size >= 10.5 or "BoldCapt" in l.font
                       and l.spans[0].text.strip().startswith(("–", "-", "–"))]
        quote_ids = {id(l) for l in quote_lines}
        # attribution line ("– Julinel Garpe, farm wife") follows the quote
        desc_lines = [l for l in text_lines if id(l) not in quote_ids and not (
            "SemiboldIt" in l.font and l.size >= 10.5)]
        description = ctx.fix(body_text(desc_lines))
        quote = ctx.fix(body_text(quote_lines)) or None

        # boxes
        groups = group_box_lines(pool)
        titles = [l for l in pool if _is_title(l) and l.text not in NON_VARIANT_TITLES]
        statblocks: list[dict] = []
        ability_groups: list[list[tuple[str, list[Line]]]] = []
        box_prefix: list[Line] = []
        plain_groups: list[Line] = []
        geo = uses_geometry(sec) and sec.anchor is not None
        if geo:
            for g, prefix, fields in geo_blocks.get(sec.id, []):
                owner = ctx.owner.get(id(g[0]))
                variant = owner.title if owner is not None and owner.kind == "statblock" and owner.anchor else None
                statblocks.append(dict(variant=variant, fields=fields, page=g[0].page))
                if prefix and not box_prefix:
                    box_prefix = prefix
        for g in groups:
            prefix, fields = parse_fields(g)
            if not fields:
                plain_groups += g           # a box that is only prose (adventure creature description)
                continue
            if is_statblock(fields):
                if geo:
                    continue                # claimed by geometry above
                owner = ctx.owner.get(id(g[0]))
                variant = owner.title if owner is not None and owner.kind == "statblock" and owner.anchor else None
                if variant is None:
                    above = [t for t in titles if t.page == g[0].page and 0 < g[0].y0 - t.y0 < 70
                             and abs((t.x0 + t.x1) / 2 - (min(x.x0 for x in g) + max(x.x1 for x in g)) / 2) < 110]
                    if above:
                        variant = max(above, key=lambda t: t.y0).text.title()
                statblocks.append(dict(variant=variant, fields=fields, page=g[0].page))
                if prefix and not box_prefix:
                    box_prefix = prefix
            else:
                ability_groups.append(fields)
        if not description and (box_prefix or plain_groups):
            description = ctx.fix(body_text(box_prefix or plain_groups))

        def prose(s: Section | None) -> str | None:
            if s is None:
                return None
            keep = [l for l in s.body_lines if id(l) not in ctx.consumed and l.font.startswith("MinionPro")]
            return ctx.fix(body_text(keep))

        mid = con.execute(
            "INSERT INTO monsters(source_id,name,kind,category,description,quote,random_encounter,adventure_seed,"
            "page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?,?)",
            (sid, sec.title, kind, category, description, quote, prose(enc), prose(seed),
             sec.page_start, ctx.printed.get(sec.page_start), ctx.link(sec.page_start))).lastrowid
        monster_ids[sec.id] = mid
        stats["monsters"] += 1

        # stat blocks
        for i, sb in enumerate(statblocks):
            fields = {lb: ctx.fix(join_lines(vals)) for lb, vals in sb["fields"]}
            norm = {norm_label(k): v for k, v in fields.items()}

            def pick(*names: str) -> str | None:
                for n in names:
                    if n in norm:
                        return norm[n]
                return None

            dbonus = pick("damage bonus str", "damage bonus", "damage bonus agl")
            con.execute(
                "INSERT INTO monster_statblocks(monster_id,ord,variant,ferocity,size,movement,armor,hp,wp,"
                "damage_bonus,skills,weapons,fields,page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (mid, i, sb["variant"], pick("ferocity"), pick("size"), pick("movement", "mov"),
                 pick("armor", "typical armor"), pick("hp"), pick("wp"), dbonus, pick("skills", "skill"),
                 pick("typical weapon", "typical weapons", "weapons", "weapon"),
                 json.dumps(fields, ensure_ascii=False), sb["page"], ctx.printed.get(sb["page"]),
                 ctx.link(sb["page"])))
            stats["statblocks"] += 1

        # abilities: labelled paragraphs outside the stat blocks + PC abilities
        n = 0
        for fields in ability_groups:
            for lb, vals in fields:
                pg = vals[0].page if vals else sec.page_start
                con.execute("INSERT INTO monster_abilities(monster_id,ord,name,kind,text,page,printed_page,pdf_link)"
                            " VALUES (?,?,?,?,?,?,?,?)",
                            (mid, n, lb, "ability", ctx.fix(join_lines(vals)), pg, ctx.printed.get(pg), ctx.link(pg)))
                n += 1
                stats["abilities"] += 1
        for d in subtree:
            if d.kind == "pc_ability":
                body = ctx.fix(body_text([l for l in d.body_lines if id(l) not in ctx.consumed]))
                con.execute("INSERT INTO monster_abilities(monster_id,ord,name,kind,text,page,printed_page,pdf_link)"
                            " VALUES (?,?,?,?,?,?,?,?)",
                            (mid, n, d.title, "pc_ability", body, d.page_start, ctx.printed.get(d.page_start),
                             ctx.link(d.page_start)))
                n += 1
                stats["abilities"] += 1

        expect[mid] = (sec, sum(1 for d in subtree if d.kind == "statblock" and d.title.lower() != "abilities"),
                       any(norm_label(d.title) == "monster attacks" for d in subtree))

        # stats that live in another book ("stats as per page 87 in the Rulebook")
        ref = STATS_REF_RX.search(description) or (None if statblocks else BOOK_REF_RX.search(description))
        if ref:
            con.execute("UPDATE monsters SET stats_ref=? WHERE id=?", (clean(ref.group(0)), mid))
        elif not statblocks:
            ctx.warn(f"monster '{sec.title}' (p{sec.page_start}): no stat block and no stats reference")

        # art
        art = None
        dest = IMAGE_DIR / ctx.key / f"{slug(sec.title)}-{mid}"
        if ctx.key == "adventure":
            if sec.anchor is not None:
                art = extract_portrait(ctx, sec.anchor, dest)
        else:
            art = extract_art(ctx, sec.page_start, last_page, dest)
        if art:
            path, pg = art
            con.execute("UPDATE monsters SET image=?, image_page=?, image_pdf_link=? WHERE id=?",
                        (path, pg, ctx.link(pg), mid))
            stats["images"] += 1

    # tables that belong to a monster: attacks and the rest (first names, ...)
    sec_by_id = {s.id: s for s in secs}
    for t in ctx.tables:
        sec = ctx.table_section.get(id(t))
        is_attack = bool(t.dice) and [c.upper() for c in t.columns] == ["ATTACK"]
        cur = sec
        owner_mid = None
        if is_attack and anchors_by_page.get(t.page):
            # wide attack tables sit beside/above their creature's title in reading order: use geometry
            best = geo_choose(anchors_by_page[t.page], t.header_line.x0 + 120, t.header_line.y0)
            if best is not None:
                owner_mid = monster_ids.get(best.id)
        while owner_mid is None and cur is not None:
            if cur.id in monster_ids:
                owner_mid = monster_ids[cur.id]
                break
            cur = sec_by_id.get(cur.parent_id) if cur.parent_id else None
        if owner_mid is None:
            continue
        if is_attack:
            con.execute("UPDATE monsters SET attack_dice=? WHERE id=?", (t.dice, owner_mid))
            tid = ctx.table_db_ids.get(id(t))           # type: ignore[attr-defined]
            if tid:
                mname = con.execute("SELECT name FROM monsters WHERE id=?", (owner_mid,)).fetchone()[0]
                con.execute("UPDATE game_tables SET monster_id=?, title=? WHERE id=?",
                            (owner_mid, f"{mname}: Monster attacks", tid))
            for i, r in enumerate(t.rows):
                text = ctx.fix(r.cells[0])
                m = ATTACK_NAME_RX.match(text)
                name = m.group(1) if m else None
                if name is None:
                    ctx.warn(f"attack without name: p{t.page} roll {r.roll_text}: {text[:50]!r}")
                con.execute(
                    "INSERT INTO monster_attacks(monster_id,ord,roll_min,roll_max,roll_text,name,text,page,printed_page,pdf_link)"
                    " VALUES (?,?,?,?,?,?,?,?,?,?)",
                    (owner_mid, i, r.roll_min, r.roll_max, r.roll_text, name, text, r.page,
                     ctx.printed.get(r.page), ctx.link(r.page)))
                stats["attacks"] += 1
        else:
            tid = ctx.table_db_ids.get(id(t))           # type: ignore[attr-defined]
            if tid:
                con.execute("UPDATE game_tables SET monster_id=? WHERE id=?", (owner_mid, tid))

    # creatures that the Rulebook prints as table rows: Common Animals and Typical NPCs
    if ctx.key == "rulebook":
        typed = {"MOVEMENT": "movement", "HP": "hp", "WP": "wp", "DAMAGE BONUS": "damage_bonus", "SKILLS": "skills",
                 "ATTACK": "weapons"}
        for t in ctx.tables:
            cols = [c.upper() for c in t.columns]
            if cols[:2] == ["ANIMAL", "MOVEMENT"]:
                kind, category = "animal", "Common Animals"
            elif cols[:2] == ["TYPE", "SKILLS"] and "HEROIC ABILITIES" in cols:
                kind, category = "npc", "Typical NPCs"
            else:
                continue
            for r in t.rows:
                cells = [ctx.fix(c) for c in r.cells]
                name = cells[0]
                mid = con.execute(
                    "INSERT INTO monsters(source_id,name,kind,category,description,page,printed_page,pdf_link)"
                    " VALUES (?,?,?,?,?,?,?,?)",
                    (sid, name, kind, category, "", t.page, ctx.printed.get(t.page), ctx.link(t.page))).lastrowid
                fields = {c.title(): v for c, v in zip(t.columns[1:], cells[1:]) if v}
                vals = {typed[c]: cells[i] for i, c in enumerate(cols) if c in typed and i > 0 and cells[i]}
                con.execute(
                    "INSERT INTO monster_statblocks(monster_id,ord,variant,movement,hp,wp,damage_bonus,skills,weapons,"
                    "fields,page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (mid, 0, None, vals.get("movement"), vals.get("hp"), vals.get("wp"), vals.get("damage_bonus"),
                     vals.get("skills"), vals.get("weapons"), json.dumps(fields, ensure_ascii=False),
                     t.page, ctx.printed.get(t.page), ctx.link(t.page)))
                stats["statblocks"] += 1
                stats["monsters"] += 1

    # cross-check against what the book's own index announces
    for mid, (sec, n_stat, has_atk) in expect.items():
        got_stat = con.execute("SELECT COUNT(*) FROM monster_statblocks WHERE monster_id=?", (mid,)).fetchone()[0]
        got_atk = con.execute("SELECT COUNT(*) FROM monster_attacks WHERE monster_id=?", (mid,)).fetchone()[0]
        if n_stat and got_stat != n_stat:
            ctx.warn(f"'{sec.title}' p{sec.page_start}: index lists {n_stat} stat block(s), extracted {got_stat}")
        if has_atk and got_atk == 0:
            ctx.warn(f"'{sec.title}' p{sec.page_start}: index lists Monster Attacks, extracted none")
        if got_atk:
            rolls = con.execute("SELECT roll_min, roll_max FROM monster_attacks WHERE monster_id=? ORDER BY ord",
                                (mid,)).fetchall()
            die = int(con.execute("SELECT attack_dice FROM monsters WHERE id=?", (mid,)).fetchone()[0][1:])
            covered = [n for a, b in rolls for n in range(a, b + 1)]
            if covered != list(range(1, die + 1)):
                ctx.warn(f"'{sec.title}' p{sec.page_start}: attack rolls cover {covered}, expected 1..{die}")

    # search index
    for mid, name, desc in con.execute("SELECT id,name,description FROM monsters WHERE source_id=?", (sid,)).fetchall():
        atk = " ".join(r[0] for r in con.execute("SELECT text FROM monster_attacks WHERE monster_id=?", (mid,)))
        con.execute("INSERT INTO search_index(kind,ref_id,title,body,source,page) VALUES (?,?,?,?,?,?)",
                    ("monster", mid, name, f"{desc}\n{atk}", ctx.key,
                     con.execute("SELECT page FROM monsters WHERE id=?", (mid,)).fetchone()[0]))
    print(f"[{ctx.key}] monsters: {stats}")
