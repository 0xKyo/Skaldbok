"""Sanity report for data/skaldbok.db.  Usage: python tools/validate.py [db]"""
import re
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
db = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "data" / "skaldbok.db")
con = sqlite3.connect(db)
problems = 0


def check(label: str, ok: bool, detail: str = "") -> None:
    global problems
    print(("  ok   " if ok else "  FAIL ") + label + (f"  {detail}" if detail and not ok else ""))
    problems += 0 if ok else 1


print("== rows per source")
for key, in con.execute("SELECT key FROM sources"):
    row = con.execute("""SELECT
        (SELECT COUNT(*) FROM sections    WHERE source_id=s.id),
        (SELECT COUNT(*) FROM game_tables WHERE source_id=s.id),
        (SELECT COUNT(*) FROM monsters    WHERE source_id=s.id),
        (SELECT COUNT(*) FROM monster_attacks a JOIN monsters m ON m.id=a.monster_id WHERE m.source_id=s.id),
        (SELECT COUNT(*) FROM monsters    WHERE source_id=s.id AND image IS NOT NULL)
        FROM sources s WHERE key=?""", (key,)).fetchone()
    print(f"  {key:10} sections={row[0]:4} tables={row[1]:3} monsters={row[2]:3} attacks={row[3]:4} with-image={row[4]:3}")

print("== text hygiene")
text_cols = [("sections", "body"), ("pages", "text"), ("game_table_rows", "cells"), ("monsters", "description"),
             ("monster_attacks", "text"), ("monster_abilities", "text"), ("spells", "description"),
             ("abilities", "description"), ("skills", "description")]
ctl = re.compile(r"[\x00-\x08\x0b-\x1f]")
for t, c in text_cols:
    bad = [r[0] for r in con.execute(f"SELECT rowid FROM {t}") if False]
    n = sum(1 for (v,) in con.execute(f"SELECT {c} FROM {t}") if v and ctl.search(v))
    check(f"{t}.{c}: no control characters / unresolved line-break markers", n == 0, f"{n} rows")

print("== structure")
one = lambda q: con.execute(q).fetchone()[0]
check("dice tables cover their whole die",
      one("""SELECT COUNT(*) FROM game_tables t WHERE dice IS NOT NULL AND
             (SELECT MIN(roll_min) FROM game_table_rows WHERE table_id=t.id) <> 1""") == 0)
check("every monster attack has a name", one("SELECT COUNT(*) FROM monster_attacks WHERE name IS NULL") == 0)
check("every monster row links to its page", one("SELECT COUNT(*) FROM monsters WHERE pdf_link NOT LIKE '%#page=%'") == 0)
check("44 heroic abilities", one("SELECT COUNT(*) FROM abilities WHERE type='heroic'") == 44)
check("6 kin / 10 professions / 6 conditions",
      (one("SELECT COUNT(*) FROM kin"), one("SELECT COUNT(*) FROM professions"), one("SELECT COUNT(*) FROM conditions")) == (6, 10, 6))
check("49 spells and 17 magic tricks",
      (one("SELECT COUNT(*) FROM spells WHERE is_trick=0"), one("SELECT COUNT(*) FROM spells WHERE is_trick=1")) == (49, 17))
check("no stat block field swallowed a banner heading",
      one("""SELECT COUNT(*) FROM monster_statblocks WHERE fields GLOB '*MONSTER ATTACKS*'
             OR fields GLOB '*RANDOM ENCOUNTER*' OR fields GLOB '*ADVENTURE SEED*'""") == 0)
check("hit points are numeric wherever a stat block has them",
      one("""SELECT COUNT(*) FROM monster_statblocks WHERE hp IS NOT NULL
             AND hp NOT GLOB '[0-9]*' AND hp NOT LIKE 'No. of%' AND hp NOT LIKE '%PC%'""") == 0)
check("every image file exists",
      all((ROOT / p).exists() for (p,) in con.execute("SELECT image FROM monsters WHERE image IS NOT NULL")))

print("== creatures without a stat block (the book gives none, or points to another book)")
for name, src, ref in con.execute("""SELECT m.name, s.key, m.stats_ref FROM monsters m JOIN sources s ON s.id=m.source_id
        WHERE NOT EXISTS (SELECT 1 FROM monster_statblocks b WHERE b.monster_id=m.id) ORDER BY s.id, m.page"""):
    print(f"  [{src}] {name}" + (f"  -> {ref[:70]}" if ref else ""))

print(f"\n{problems} problem(s)")
sys.exit(1 if problems else 0)
