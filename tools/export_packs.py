"""Exports the Core content of data/dragonbane.db as a JSON content pack: data/packs/core/.

The app loads Core exactly like any homebrew pack (see docs/HOMEBREW.md for the format), so this file is
also the reference for what a pack looks like. Book text, page images and generic tables stay in the SQLite
database; this pack holds the game content: creatures, spells, abilities, skills, kin, professions, equipment
and the few tables the character creator rolls on.

Usage: python tools/export_packs.py [db]        (build_db.py runs it automatically)
"""
from __future__ import annotations

import json
import re
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACK_DIR = ROOT / "data" / "packs" / "core"
FORMAT = 1

SHORT_NAMES = {"rulebook": "Rulebook", "bestiary": "Bestiary", "adventure": "Adventure"}

# The six attributes a skill can be based on. The weapon skills' attributes are not in the running text of the
# Rulebook; they are printed on its character sheet (page 127: "Axes (STR)", "Bows (AGL)"...).
WEAPON_SKILL_ATTRIBUTE = {
    "Axes": "STR", "Bows": "AGL", "Brawling": "STR", "Crossbows": "AGL", "Hammers": "STR",
    "Knives": "AGL", "Slings": "AGL", "Spears": "STR", "Staves": "AGL", "Swords": "STR",
}

# Tables of the Rulebook the character creator rolls on (title -> role). They are also in the database, so the
# pack marks them "browse": false to keep them out of the Tables list twice.
ROLE_TABLES = {"Weakness": "weakness", "Memento": "memento", "Appearance": "appearance"}


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower().replace("&", " and ")).strip("-")


def clean(d: dict) -> dict:
    """Drops empty values so the JSON only carries what the book says."""
    return {k: v for k, v in d.items() if v not in (None, "", [], {})}


def write(name: str, key: str, rows: list[dict]) -> None:
    PACK_DIR.mkdir(parents=True, exist_ok=True)
    (PACK_DIR / name).write_text(
        json.dumps({"format": FORMAT, key: rows}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(f"  {name}: {len(rows)}")


class Ids:
    """Stable, unique slugs per file."""

    def __init__(self) -> None:
        self.used: set[str] = set()

    def make(self, base: str) -> str:
        base = slug(base) or "entry"
        cand, n = base, 2
        while cand in self.used:
            cand = f"{base}-{n}"
            n += 1
        self.used.add(cand)
        return cand


def main(db_path: str) -> int:
    con = sqlite3.connect(db_path)
    con.row_factory = sqlite3.Row
    src = {r["id"]: r["key"] for r in con.execute("SELECT id, key FROM sources")}
    sources = [dict(key=r["key"], title=r["title"], short=SHORT_NAMES.get(r["key"], r["title"]), file=r["file"],
                    page_offset=r["page_offset"]) for r in con.execute("SELECT * FROM sources ORDER BY id")]
    print(f"exporting Core pack to {PACK_DIR}")
    PACK_DIR.mkdir(parents=True, exist_ok=True)
    (PACK_DIR / "manifest.json").write_text(json.dumps({
        "format": FORMAT, "id": "core", "name": "Dragonbane Core", "version": "1.0.0", "author": "Free League",
        "description": "Content extracted from the Dragonbane books (Rulebook, Bestiary, The Misty Vale).",
        "core": True, "sources": sources}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

    def page_fields(r) -> dict:
        return {"page": r["page"], "printed_page": r["printed_page"]}

    # ---------------------------------------------------------------- spells
    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM spells ORDER BY school, is_trick DESC, CAST(rank AS INTEGER), name"):
        rows.append(clean(dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]], school=r["school"],
                               trick=bool(r["is_trick"]), rank=r["rank"], prerequisite=r["prerequisite"],
                               requirement=r["requirement"], casting_time=r["casting_time"], range=r["range"],
                               duration=r["duration"], description=r["description"], **page_fields(r))))
        if not r["is_trick"]:
            rows[-1]["trick"] = False
    write("spells.json", "spells", rows)

    # ---------------------------------------------------------------- abilities
    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM abilities ORDER BY type DESC, name"):
        rows.append(clean(dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]], type=r["type"],
                               kin=r["kin"], requirement=r["requirement"], wp_cost=r["wp_cost"],
                               description=r["description"], **page_fields(r))))
    write("abilities.json", "abilities", rows)

    # ---------------------------------------------------------------- skills
    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM skills ORDER BY CASE category WHEN 'core' THEN 0 WHEN 'weapon' THEN 1 ELSE 2 END, name"):
        attribute = r["attribute"] or WEAPON_SKILL_ATTRIBUTE.get(r["name"])
        if r["name"] == "Weapon Skills":       # the heading of the weapon skills ("STR/AGL"), not a skill of its own
            attribute = "STR/AGL"
        rows.append(clean(dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]],
                               attribute=attribute, category=r["category"], description=r["description"],
                               **page_fields(r))))
    write("skills.json", "skills", rows)

    # ---------------------------------------------------------------- kin
    movement = {}
    for t in con.execute("SELECT id FROM game_tables WHERE source_id=1 AND title='Movement' AND columns LIKE '%KIN%'"):
        for row in con.execute("SELECT cells FROM game_table_rows WHERE table_id=?", (t["id"],)):
            cells = json.loads(row["cells"])
            movement[cells[0]] = int(cells[1])

    def table_rows(title: str, source: int = 1) -> list[str]:
        t = con.execute("SELECT id FROM game_tables WHERE source_id=? AND title=?", (source, title)).fetchone()
        if not t:
            return []
        return [json.loads(r["cells"])[0] for r in
                con.execute("SELECT cells FROM game_table_rows WHERE table_id=? ORDER BY ord", (t["id"],))]

    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM kin ORDER BY id"):
        innate = [a["name"] for a in con.execute("SELECT name FROM abilities WHERE type='kin' AND kin=? ORDER BY id", (r["name"],))]
        rows.append(clean(dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]],
                               description=r["description"], movement=movement.get(r["name"]),
                               innate_abilities=innate, names=table_rows(f"{r['name']}: First Name"),
                               **page_fields(r))))
    write("kin.json", "kin", rows)

    # ---------------------------------------------------------------- professions
    ability_names = {a["name"] for a in con.execute("SELECT name FROM abilities")}
    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM professions ORDER BY name"):
        skills = [s.strip() for s in (r["skills"] or "").split(",") if s.strip()]
        heroic = re.split(r",\s*|\s+or\s+", r["heroic_ability"] or "")
        heroic = [h.strip() for h in heroic if h.strip() in ability_names]
        entry = dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]], description=r["description"],
                     key_attribute=r["key_attribute"], skills=skills, heroic_abilities=heroic,
                     starting_gear=table_rows(f"{r['name']}: Gear"), nicknames=table_rows(f"{r['name']}: Nickname"),
                     **page_fields(r))
        if not skills:       # a profession whose skill list depends on a choice: the mage's school of magic
            sec = con.execute("SELECT body FROM sections WHERE source_id=1 AND kind='section' AND title=? AND page_start=?",
                              (r["name"], r["page"] - 0)).fetchone() or con.execute(
                "SELECT body FROM sections WHERE source_id=1 AND kind='section' AND title=?", (r["name"],)).fetchone()
            by_school = {}
            for m in re.finditer(r"✦(\w+) Skills:\s*([^\n]+)", sec["body"] if sec else ""):
                school = {"Animist": "Animism", "Elementalist": "Elementalism", "Mentalist": "Mentalism"}.get(m.group(1), m.group(1))
                by_school[school] = [s.strip() for s in m.group(2).split(",") if s.strip()]
            if by_school:
                entry["skills_by_school"] = by_school
                entry["magic"] = {"spells": 3, "tricks": 3, "spell_rank": 1}
                entry["heroic_ability"] = None
        rows.append(clean(entry))
    write("professions.json", "professions", rows)

    # ---------------------------------------------------------------- equipment
    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM weapons ORDER BY kind DESC, id"):
        rows.append(clean(dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]], kind=r["kind"],
                               grip=r["grip"], str_req=r["str_req"], range=r["range"], damage=r["damage"],
                               durability=r["durability"], cost=r["cost"], supply=r["supply"], features=r["features"],
                               **page_fields(r))))
    write("weapons.json", "weapons", rows)

    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM armor ORDER BY id"):
        slot = "helmet" if re.search(r"helm", r["name"], re.I) else "armor"
        rows.append(clean(dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]], slot=slot,
                               armor_rating=r["armor_rating"], cost=r["cost"], supply=r["supply"], effect=r["effect"],
                               **page_fields(r))))
    write("armor.json", "armor", rows)

    ids, rows = Ids(), []
    for r in con.execute("SELECT * FROM gear_items ORDER BY id"):
        rows.append(clean(dict(id=ids.make(r["name"]), name=r["name"], source=src[r["source_id"]],
                               category=r["category"], cost=r["cost"], supply=r["supply"], weight=r["weight"],
                               effect=r["effect"], **page_fields(r))))
    write("gear.json", "gear", rows)

    # ---------------------------------------------------------------- tables the character creator uses
    ids, rows = Ids(), []
    for title, role in ROLE_TABLES.items():
        t = con.execute("SELECT * FROM game_tables WHERE source_id=1 AND title=?", (title,)).fetchone()
        if not t:
            print(f"  WARNING: table '{title}' not found")
            continue
        trs = list(con.execute("SELECT * FROM game_table_rows WHERE table_id=? ORDER BY ord", (t["id"],)))
        rows.append(clean(dict(id=ids.make(title), name=title, source="rulebook", role=role, browse=False,
                               dice=t["dice"], columns=json.loads(t["columns"]),
                               rows=[dict(roll=r["roll_text"], cells=json.loads(r["cells"])) for r in trs],
                               **page_fields(t))))
    write("tables.json", "tables", rows)

    # ---------------------------------------------------------------- creatures (one big file)
    ids = {k: Ids() for k in src.values()}
    rows = []
    related = {}
    for r in con.execute("SELECT monster_id, title FROM game_tables WHERE monster_id IS NOT NULL AND columns <> '[\"ATTACK\"]' ORDER BY id"):
        related.setdefault(r["monster_id"], []).append(r["title"])
    images = 0
    for m in con.execute("SELECT * FROM monsters ORDER BY source_id, page, id"):
        key = src[m["source_id"]]
        image = m["image"]
        if image:
            image = image.replace("data/packs/core/", "", 1)
            if not (PACK_DIR / image).exists():
                print(f"  WARNING: image missing for {m['name']}: {image}")
            images += 1
        blocks = []
        for b in con.execute("SELECT * FROM monster_statblocks WHERE monster_id=? ORDER BY ord", (m["id"],)):
            blocks.append(clean(dict(variant=b["variant"], fields=json.loads(b["fields"]), **page_fields(b))))
        attacks = [clean(dict(roll=a["roll_text"], name=a["name"], text=a["text"], **page_fields(a)))
                   for a in con.execute("SELECT * FROM monster_attacks WHERE monster_id=? ORDER BY ord", (m["id"],))]
        abilities = [clean(dict(name=a["name"], kind=a["kind"], text=a["text"]))
                     for a in con.execute("SELECT * FROM monster_abilities WHERE monster_id=? ORDER BY ord", (m["id"],))]
        rows.append(clean(dict(
            id=ids[key].make(f"{key}-{m['name']}"), name=m["name"], source=key, kind=m["kind"], category=m["category"],
            description=m["description"], quote=m["quote"], random_encounter=m["random_encounter"],
            adventure_seed=m["adventure_seed"], stats_ref=m["stats_ref"], attack_dice=m["attack_dice"],
            image=image, image_page=m["image_page"], statblocks=blocks, attacks=attacks, abilities=abilities,
            tables=related.get(m["id"], []), **page_fields(m))))
    write("creatures.json", "creatures", rows)
    print(f"  ({images} creatures with art in images/)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "data" / "dragonbane.db")))
