"""Typed rules content of the Rulebook: skills, abilities, kin, professions, spells, conditions, gear."""
from __future__ import annotations

import re
import sqlite3

from .build import Context, section_body, table_title
from .monsters import ancestor_chapter, descendants
from .sections import Section

FIELD_RX = re.compile(r"^✦\s*([A-Z][A-Za-z' ]{1,28}):\s*(.*)$")
ATTR_RX = re.compile(r"^\(([A-Z]{3})\)\s*")


def split_fields(body: str) -> tuple[dict[str, str], str]:
    """'✦Label: value' lines become fields; everything else stays description (order preserved)."""
    fields: dict[str, str] = {}
    rest: list[str] = []
    for line in body.split("\n"):
        m = FIELD_RX.match(line)
        if m:
            fields[m.group(1).strip()] = m.group(2).strip()
        else:
            rest.append(line)
    return fields, "\n".join(rest).strip()


def ancestors(secs_by_id: dict[int, Section], s: Section) -> list[Section]:
    out = []
    cur = s
    while cur.parent_id is not None:
        cur = secs_by_id[cur.parent_id]
        out.append(cur)
    return out


def run(ctx: Context, con: sqlite3.Connection) -> None:
    if ctx.key != "rulebook":
        return
    sid = ctx.source_id
    secs = ctx.sections
    by_id = {s.id: s for s in secs}
    counts: dict[str, int] = {}

    def common(s: Section):
        return (sid, ctx.sec_id(s))

    def loc(page: int):
        return (page, ctx.printed.get(page), ctx.link(page))

    def chapter_no(s: Section) -> int | None:
        ch = ancestor_chapter(secs, s)
        return ch.chapter_no if ch else None

    # ---- heroic abilities (chapter 3) -------------------------------------------------------
    for s in secs:
        par = by_id.get(s.parent_id) if s.parent_id else None
        if par is not None and par.title == "Heroic Abilities" and chapter_no(s) == 3 and s.level == 3:
            fields, desc = split_fields(section_body(ctx, s))
            con.execute("INSERT INTO abilities(source_id,section_id,name,type,kin,requirement,wp_cost,description,"
                        "page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                        (*common(s), s.title, "heroic", None, fields.get("Requirement"),
                         fields.get("Willpower Points"), desc, *loc(s.page_start)))
            counts["heroic abilities"] = counts.get("heroic abilities", 0) + 1
            if "Requirement" not in fields or "Willpower Points" not in fields:
                ctx.warn(f"heroic ability '{s.title}' p{s.page_start}: missing requirement/WP ({fields})")

    # ---- kin and their innate abilities (chapter 2) -----------------------------------------
    for s in secs:
        par = by_id.get(s.parent_id) if s.parent_id else None
        if par is not None and par.title == "Kin" and chapter_no(s) == 2:
            abil = [d for d in descendants(secs, s) if d.kind == "ability"]
            if not abil:
                continue
            body = section_body(ctx, s)
            con.execute("INSERT INTO kin(source_id,section_id,name,description,page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?)",
                        (*common(s), s.title, body, *loc(s.page_start)))
            counts["kin"] = counts.get("kin", 0) + 1
            for a in abil:
                fields, desc = split_fields(section_body(ctx, a))
                con.execute("INSERT INTO abilities(source_id,section_id,name,type,kin,requirement,wp_cost,description,"
                            "page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                            (*common(a), a.title, "kin", s.title, None, fields.get("Willpower Points"), desc,
                             *loc(a.page_start)))
                counts["kin abilities"] = counts.get("kin abilities", 0) + 1

    # ---- professions (chapter 2) --------------------------------------------------------------
    for s in secs:
        par = by_id.get(s.parent_id) if s.parent_id else None
        if par is not None and par.title == "Profession" and chapter_no(s) == 2:
            fields, desc = split_fields(section_body(ctx, s))
            if "Key Attribute" not in fields:
                continue
            con.execute("INSERT INTO professions(source_id,section_id,name,description,key_attribute,skills,"
                        "heroic_ability,page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?)",
                        (*common(s), s.title, desc, fields.get("Key Attribute"), fields.get("Skills"),
                         fields.get("Heroic Ability"), *loc(s.page_start)))
            counts["professions"] = counts.get("professions", 0) + 1

    # ---- skills ---------------------------------------------------------------------------------
    for s in secs:
        par = by_id.get(s.parent_id) if s.parent_id else None
        if par is None or chapter_no(s) not in (3, 5) or s.kind != "section":
            continue
        if par.title == "The Core Skills" and s.level == 3:
            body = section_body(ctx, s)
            m = ATTR_RX.match(body)
            con.execute("INSERT INTO skills(source_id,section_id,name,attribute,category,description,page,printed_page,pdf_link)"
                        " VALUES (?,?,?,?,?,?,?,?,?)",
                        (*common(s), s.title, m.group(1) if m else None,
                         "weapon" if s.title == "Weapon Skills" else "core", ATTR_RX.sub("", body), *loc(s.page_start)))
            counts["skills"] = counts.get("skills", 0) + 1
            if not m and s.title != "Weapon Skills":
                ctx.warn(f"skill '{s.title}' p{s.page_start}: attribute not found")
        elif par.title == "Weapon Skills":
            con.execute("INSERT INTO skills(source_id,section_id,name,attribute,category,description,page,printed_page,pdf_link)"
                        " VALUES (?,?,?,?,?,?,?,?,?)",
                        (*common(s), s.title, None, "weapon", section_body(ctx, s), *loc(s.page_start)))
            counts["skills"] = counts.get("skills", 0) + 1
        elif par.title == "Schools of Magic" and s.title in ("Animism", "Elementalism", "Mentalism"):
            con.execute("INSERT INTO skills(source_id,section_id,name,attribute,category,description,page,printed_page,pdf_link)"
                        " VALUES (?,?,?,?,?,?,?,?,?)",
                        (*common(s), s.title, "INT", "magic", section_body(ctx, s), *loc(s.page_start)))
            counts["skills"] = counts.get("skills", 0) + 1

    # ---- spells ---------------------------------------------------------------------------------
    for s in secs:
        if chapter_no(s) != 5 or s.kind != "section" or s.level < 4:
            continue
        chain = ancestors(by_id, s)
        titles = [a.title for a in chain]
        if "Spell List" not in titles or s.title == "Magic Tricks":
            continue
        school = next((a.title for a in chain if by_id.get(a.parent_id) is not None
                       and by_id[a.parent_id].title == "Spell List"), None)
        fields, desc = split_fields(section_body(ctx, s))
        is_trick = "Magic Tricks" in titles
        if not is_trick and "Rank" not in fields:
            continue
        con.execute("INSERT INTO spells(source_id,section_id,name,school,is_trick,rank,prerequisite,requirement,"
                    "casting_time,range,duration,description,page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (*common(s), s.title, school, int(is_trick), fields.get("Rank"), fields.get("Prerequisite"),
                     fields.get("Requirement"), fields.get("Casting Time"), fields.get("Range"), fields.get("Duration"),
                     desc, *loc(s.page_start)))
        counts["spells" if not is_trick else "magic tricks"] = counts.get("spells" if not is_trick else "magic tricks", 0) + 1
        if not is_trick and not all(k in fields for k in ("Rank", "Casting Time", "Range", "Duration")):
            ctx.warn(f"spell '{s.title}' p{s.page_start}: incomplete fields {sorted(fields)}")

    # ---- conditions -----------------------------------------------------------------------------
    for s in secs:
        if s.title == "Conditions" and chapter_no(s) == 4 and s.level == 2:
            for name, attr in re.findall(r"✦\s*([A-Z][a-z]+)\s*[–-]\s*([A-Z]{3})", section_body(ctx, s)):
                con.execute("INSERT INTO conditions(source_id,name,attribute,page,printed_page,pdf_link) VALUES (?,?,?,?,?,?)",
                            (sid, name, attr, *loc(s.page_start)))
                counts["conditions"] = counts.get("conditions", 0) + 1

    # ---- gear from the plain tables --------------------------------------------------------------
    for t in ctx.tables:
        if t.dice or not 77 <= t.page <= 85:
            continue
        cols = [c.upper() for c in t.columns]
        tid = ctx.table_db_ids.get(id(t))          # type: ignore[attr-defined]
        title = table_title(ctx, t)
        page3 = (t.page, ctx.printed.get(t.page), ctx.link(t.page))

        def col(row, *names):
            for n in names:
                if n in cols:
                    return ctx.fix(row.cells[cols.index(n)]) or None
            return None

        for r in t.rows:
            if cols[0] == "WEAPON":
                kind = "ranged" if "ranged" in title.lower() else "melee"
                con.execute("INSERT INTO weapons(source_id,table_id,kind,name,grip,str_req,range,damage,durability,cost,"
                            "supply,features,page,printed_page,pdf_link) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                            (sid, tid, kind, ctx.fix(r.cells[0]), col(r, "GRIP"), col(r, "STR"), col(r, "RANGE"),
                             col(r, "DAMAGE"), col(r, "DURABILITY"), col(r, "COST"), col(r, "SUPPLY"),
                             col(r, "FEATURES"), *page3))
                counts["weapons"] = counts.get("weapons", 0) + 1
            elif cols[0] == "ARMOR":
                con.execute("INSERT INTO armor(source_id,table_id,name,armor_rating,cost,supply,effect,page,printed_page,pdf_link)"
                            " VALUES (?,?,?,?,?,?,?,?,?,?)",
                            (sid, tid, ctx.fix(r.cells[0]), col(r, "ARMOR RATING"), col(r, "COST"), col(r, "SUPPLY"),
                             col(r, "EFFECT"), *page3))
                counts["armor"] = counts.get("armor", 0) + 1
            elif cols[0] in ("ITEM", "GARMENT", "TOOL", "SERVICE", "VEHICLE", "ANIMAL"):
                con.execute("INSERT INTO gear_items(source_id,table_id,category,name,cost,supply,weight,effect,page,printed_page,pdf_link)"
                            " VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                            (sid, tid, title, ctx.fix(r.cells[0]), col(r, "COST"), col(r, "SUPPLY"), col(r, "WEIGHT"),
                             col(r, "EFFECT", "COMMENT"), *page3))
                counts["gear items"] = counts.get("gear items", 0) + 1

    # ---- search index -----------------------------------------------------------------------------
    for table, kind, cols in (("abilities", "ability", "description"), ("spells", "spell", "description"),
                              ("skills", "skill", "description"), ("professions", "profession", "description"),
                              ("kin", "kin", "description")):
        for rid, name, body, page in con.execute(f"SELECT id,name,{cols},page FROM {table} WHERE source_id=?", (sid,)).fetchall():
            con.execute("INSERT INTO search_index(kind,ref_id,title,body,source,page) VALUES (?,?,?,?,?,?)",
                        (kind, rid, name, body, ctx.key, page))
    print(f"[rulebook] rules: {counts}")
