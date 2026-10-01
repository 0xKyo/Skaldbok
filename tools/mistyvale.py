"""Parses the Misty Vale adventure (References/MistyValeAdventure.pdf) into a content pack: data/packs/mistyvale/.

What it writes (all YAML):
  manifest.yaml    the pack and the PDF it comes from (page links)
  adventure.yaml   the book as a tree: chapters, sections, locations (numbered), NPCs, monsters, sidebars, maps, tables
  tables.yaml      the adventure's tables (random events, rumors, random encounters...), in the format of docs/HOMEBREW.md

The creatures and NPCs themselves (their stat blocks and attack tables) are already in a pack (the named NPCs in
data/packs/mistyvale/creatures.yaml, the groups and monsters in Core's creatures.yaml, "source: adventure"), so the NPC and monster nodes of the tree only point at them ("creatures: [mistyvale/monster/hardy]").

Usage:  python tools/mistyvale.py            (needs pymupdf; see tools/requirements.txt)
"""
from __future__ import annotations

import json
import re
import shutil
import sys
import warnings
from pathlib import Path

warnings.filterwarnings("ignore")
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pymupdf  # noqa: E402
from skaldbok import build  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "packs" / "mistyvale"
# Where the adventure's creatures are: the named NPCs in this pack, the groups and monsters still in Core (pack id, file)
CREATURES = [("mistyvale", ROOT / "data" / "packs" / "mistyvale" / "creatures.yaml"), ("core", ROOT / "data" / "packs" / "core" / "creatures.yaml")]
PACK_ID = "mistyvale"
SOURCE_KEY = "adventure"

# Tables that are the attacks of a creature: they are part of the creature in Core, not of the adventure.
ATTACK_TITLES = re.compile(r"^(monster attacks|.*: attack)$", re.I)
# Tables whose title says nothing without the chapter they belong to.
GENERIC_TITLES = {"random events", "random encounters", "rumors"}
# Bookmarks that are only a label on a map or a stat block: no text of their own
EMPTY_KINDS = {"statblock"}


# The big letter that opens each chapter is a picture, not text: these were read off the rendered pages (physical page -> letter).
DROP_CAPS = {5: "W", 13: "T", 16: "T", 30: "B", 35: "D", 41: "A", 49: "T", 57: "O", 62: "A ", 69: "O", 76: "O", 81: "T", 88: "S", 97: "R", 104: "O"}

# ------------------------------------------------------------------------------------------------ small YAML writer
_PLAIN = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9 _/().,'’+\-]*$")
_RESERVED = {"true", "false", "null", "yes", "no", "on", "off", "~"}


def scalar(value) -> str:
    if value is True:
        return "true"
    if value is False:
        return "false"
    if value is None:
        return "null"
    if isinstance(value, (int, float)):
        return str(value)
    s = str(value)
    if (_PLAIN.match(s) and s.lower() not in _RESERVED and not re.match(r"^[-+]?[\d.]+$", s) and not s.endswith(" ")
            and ": " not in s and " #" not in s):
        return s
    return json.dumps(s, ensure_ascii=False)


def emit(value, indent: int = 0, out: list[str] | None = None) -> list[str]:
    out = [] if out is None else out
    pad = " " * indent
    if isinstance(value, dict):
        for key, item in value.items():
            if isinstance(item, (dict, list)) and item:
                out.append(f"{pad}{key}:")
                emit(item, indent + 2, out)
            elif isinstance(item, (dict, list)):
                out.append(f"{pad}{key}: {'{}' if isinstance(item, dict) else '[]'}")
            elif isinstance(item, str) and "\n" in item and not any(l.startswith((" ", "\t")) or l != l.rstrip() for l in item.split("\n")):
                out.append(f"{pad}{key}: |")
                out.extend(f"{pad}  {line}" if line else "" for line in item.split("\n"))
            else:
                out.append(f"{pad}{key}: {scalar(item)}")
    else:  # list
        for item in value:
            if isinstance(item, dict) and item:
                lines = emit(item, indent + 2)
                lines[0] = f"{pad}- {lines[0].lstrip()}"
                out.extend(lines)
            elif isinstance(item, (dict, list)):
                out.append(f"{pad}- {'{}' if isinstance(item, dict) else '[]'}")
            else:
                out.append(f"{pad}- {scalar(item)}")
    return out


def write_yaml(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(emit(data)) + "\n", encoding="utf-8", newline="\n")


# ------------------------------------------------------------------------------------------------ text helpers
def slug(text: str) -> str:
    text = text.lower().replace("’", "").replace("'", "").replace("&", " and ")
    return re.sub(r"[^a-z0-9]+", "-", text).strip("-")


def norm(text: str) -> str:
    text = text.lower().replace("’", "'")
    text = re.sub(r"^(the|a|an)\s+", "", text)
    return re.sub(r"[^a-z0-9]+", " ", text).strip()


def tidy(text: str) -> str:
    """Body text as it is read: no trailing spaces, no runs of blank lines."""
    text = re.sub("[    ]", " ", text)             # the book's thin and wide spaces
    lines = [l.rstrip() for l in text.replace("\r", "").split("\n")]
    lines = [l for l in lines if not re.fullmatch(r"\d+(?:\s+\d+)*", l.strip())]      # the numbers of a map's key, read off the artwork
    out: list[str] = []
    for l in lines:
        if not l and (not out or not out[-1]):
            continue
        out.append(l)
    while out and not out[-1]:
        out.pop()
    return "\n".join(out)


class CreatureIndex:
    """The adventure's creatures that Core already has, by name. Two creatures can share a name (the Demon Cultists of two chapters):
    the one whose category is the chapter being read wins."""

    def __init__(self, by_name: dict[str, list[tuple[str, str]]]):
        self.by_name = by_name
        self.chapter = ""

    def __contains__(self, name: str) -> bool:
        return name in self.by_name

    def __getitem__(self, name: str) -> str:
        options = self.by_name[name]
        here = [key for key, category in options if category == norm(self.chapter)]
        return (here or [options[0][0]])[0]


def core_creatures() -> CreatureIndex:
    found: dict[str, list[tuple[str, str]]] = {}
    cid = name = source = category = None
    lines = [(pack, line) for pack, path in CREATURES if path.exists() for line in path.read_text(encoding="utf-8").splitlines()]
    for pack, line in lines:
        m = re.match(r"^  - id: (.+)$", line)
        if m:
            cid, name, source, category = m.group(1).strip(), None, None, None
            continue
        if not cid:
            continue
        for key, rx in (("name", r"^    name: (.+)$"), ("source", r"^    source: (.+)$"), ("category", r"^    category: (.+)$")):
            m = re.match(rx, line)
            if m:
                value = m.group(1).strip().strip('"')
                name, source, category = (value if key == "name" else name), (value if key == "source" else source), (value if key == "category" else category)
        if name and source and category is not None:
            if source == SOURCE_KEY:
                found.setdefault(norm(name), []).append((f"{pack}/monster/{cid}", norm(category)))
            cid = None
    return CreatureIndex(found)



# ------------------------------------------------------------------------------------------------ tables the extractor misses
ROLL = r"\d+(?:\s*[–-]\s*\d+)?"


def raw_roll_table(doc, page: int) -> list[tuple[str, str]]:
    """A 'ROLL EVENT' table read from the page's text flow, for the tables the layout extractor could not split.
    A row begins with its roll, alone on its line or followed by the text; rolls must follow each other (a '2 gold coins' inside a
    row is not a new row). The table ends at the next heading in capitals."""
    lines = [l.strip() for l in doc[page - 1].get_text().split("\n")]
    start = next((i for i, l in enumerate(lines) if re.fullmatch(r"ROLL\s+EVENT", l)), None)
    if start is None:
        return []
    rows: list[tuple[str, list[str]]] = []
    expected = 1
    for l in lines[start + 1:]:
        if not l:
            continue
        m = re.fullmatch(rf"({ROLL})", l) or re.match(rf"^({ROLL})\s+(\S.*)$", l)
        if m:
            lo = int(re.split(r"\s*[–-]\s*", m.group(1))[0])
            if lo == expected:
                hi = int(re.split(r"\s*[–-]\s*", m.group(1))[-1])
                expected = hi + 1
                rows.append((re.sub(r"\s*[–-]\s*", "–", m.group(1)), [m.group(2)] if m.lastindex == 2 else []))
                continue
        if rows and (l.isupper() and len(l) > 3 or l.startswith("✦") or l.startswith("CHAPTER")):
            break
        if rows:
            rows[-1][1].append(l)
    return [(roll, join_wrapped(parts)) for roll, parts in rows]


def join_wrapped(parts: list[str]) -> str:
    text = ""
    for p in parts:
        if text.endswith(("-", "­")) and not text.endswith(" -") and p[:1].islower():
            text = text[:-1] + p                      # a hyphen at the end of a line: the word goes on
        else:
            text = f"{text} {p}" if text else p
    return re.sub(r"\s+", " ", text.replace("­", "")).strip()


LABEL = re.compile(rf"^({ROLL}|First|Second|Third|Fourth|Fifth)" + chr(92) + "b" + chr(92) + "s*(.*)$", re.S)


def block_table(doc, page: int, heading: str) -> tuple[list[str], list[tuple[str, str]]]:
    """A small table read from the page's text blocks: a heading in capitals, maybe a header line with the columns, then one block per
    row that begins with its roll ('1-4') or its order ('First'). A block indented further than the rows continues the row above."""
    blocks = [b for b in doc[page - 1].get_text("blocks") if b[4].strip()]
    start = next((i for i, b in enumerate(blocks) if b[4].strip().upper() == heading.upper()), None)
    if start is None:
        return [], []
    right = blocks[start][0] >= 306.0                  # which column of the page the table is in
    columns: list[str] = []
    rows: list[tuple[str, list[str]]] = []
    row_x = None
    for b in blocks[start + 1:]:
        text = join_wrapped([l.strip() for l in b[4].strip().split("\n")])
        if (b[0] >= 306.0) != right:
            break
        if text.isupper() and rows:
            break
        m = LABEL.match(text)
        if m and (row_x is None or b[0] <= row_x + 8):
            row_x = b[0] if row_x is None else row_x
            rows.append((re.sub(r"\s*[–-]\s*", "–", m.group(1)), [m.group(2)] if m.group(2) else []))
        elif rows:
            rows[-1][1].append(text)                  # indented: the rest of the row above
        elif not columns:
            columns = [text]
    return columns, [(roll, join_wrapped(parts)) for roll, parts in rows]


# A few drop caps the picture detector does not reach: the start of the text -> the letter that is missing
TEXT_FIXES = {"n a mountaintop in the Misty Vale": "O"}
# A stat block whose name is printed below it, in the next section's heading: (section, page) -> the creature
STRAY_BLOCKS = {("Locations", 50): "Demon Cultists"}
# Parts of the book that are not the adventure
SKIP_CHAPTERS = {"Contents", "Index"}
_LABEL = re.compile(r"^[A-Z0-9][A-Z0-9 '’&–-]{2,}$")


def strip_labels(body: str) -> str:
    """Drops the all-capitals lines (map labels, a list of chapter names) at the end of a text: they come from the artwork."""
    lines = body.split("\n")
    while lines and (not lines[-1].strip() or _LABEL.match(lines[-1].strip())):
        lines.pop()
    return "\n".join(lines)


_STAT = re.compile(r"^(Ferocity|Size|Movement|Damage Bonus|Armor|HP|WP|Skills|Abilities|Weapons?|Typical Weapon|Attack|Gear|Equipment|Spells|Magic)\s*:")


def split_stats(body: str) -> tuple[str, str]:
    """A creature's text, without its stat block (that is in Core with the creature), and what comes after the block: the text of the
    place it is in (✦ exits, other notes), which the book prints after the stat block."""
    lines = body.split("\n")
    first = next((i for i, l in enumerate(lines) if _STAT.match(l)), None)
    if first is None:
        return body, ""
    end = first
    while end < len(lines) and _STAT.match(lines[end]):
        end += 1
    return "\n".join(lines[:first]).strip(), "\n".join(lines[end:]).strip()


def inline_stats(body: str, creatures: CreatureIndex, hint: str = "") -> tuple[str, list[str], list[str]]:
    """Stat blocks of creatures printed inside the text of another section (a sidebar, a room): the block goes, and the creature
    named in capitals next to it (before the description, or after the block) is linked instead. A block whose creature is not in
    Core stays where it is. Returns the new text, the keys linked and the captions that matched nothing."""
    lines = body.split("\n")
    out: list[str] = []
    keys: list[str] = []
    unmatched: list[str] = []
    i = 0
    while i < len(lines):
        if not (_STAT.match(lines[i]) and lines[i].startswith(("Movement", "Ferocity", "Size"))):
            out.append(lines[i])
            i += 1
            continue
        j = i
        while j < len(lines) and _STAT.match(lines[j]):
            j += 1
        after = lines[j].strip() if j < len(lines) else ""
        before = next((l.strip() for l in reversed(out[-4:]) if _LABEL.match(l.strip())), "")
        caption = next((c for c in (after, before) if c and _LABEL.match(c) and norm(c) in creatures), "")
        if not caption and hint and norm(hint) in creatures:
            keys.append(creatures[norm(hint)])                     # the caption of the block is not in this text: the book says whose it is
            i = j
            continue
        if caption:
            keys.append(creatures[norm(caption)])
            i = j + 1 if caption == after else j                  # the caption after the block goes with it
        else:
            unmatched.append(after or before or "?")
            out.extend(lines[i:j])
            i = j
    return "\n".join(out).strip(), list(dict.fromkeys(keys)), unmatched


def ends_sentence(text: str) -> bool:
    return text.rstrip().endswith((".", "!", "?", "”", '"', ":", ")", "…"))


# ------------------------------------------------------------------------------------------------ keywords the page turns into links
# A term repeated at least this many times in the book is worth a link when it names something that has a page of its own.
MIN_REPEATS = 5
# Where a term goes when its own title does not say it: term -> (kind, title) of the node. These are choices, change them here.
KEYWORD_TARGETS = {
    "Um-Durman": ("location", "The Crypt of Um-Durman"),
    "Azrahel Koth": ("monster", "Azrahel Koth"),
    "Eledain": ("section", "Eledain’s Empire"),
    "Dragon Emperor": ("section", "Eledain’s Empire"),
    "Blackridge": ("chapter", "The Village of the Day Before"),
}
_PROPER = re.compile(r"\b[A-Z][\w’'\-]*(?:(?:\s+(?:of|the|and)\s+|\s+|-)[A-Z][\w’'\-]*)*")
# who matters first when two pages could take the same term
_PRIORITY = {"chapter": 0, "location": 1, "npc": 2, "monster": 2, "section": 3, "sidebar": 4, "table": 5, "map": 6}


def assign_keywords(flat: list[dict]) -> tuple[int, list[str]]:
    """Gives each page the words of the text that should lead to it (`keywords:`): its own title (places, people), the names of its
    people, and the proper names the book repeats (Um-Durman, Azrahel Koth...) that name something with a page. Returns how many
    words were assigned and the frequent names that no page takes (to decide by hand in KEYWORD_TARGETS)."""
    corpus = "\n".join(n.get("body", "") for n in flat)
    lower = corpus.lower()
    claimed: dict[str, dict] = {}

    def lowercase_is_a_word(term: str) -> bool:
        return term.lower() != term and re.search(rf"(?<![\w’']){re.escape(term.lower())}(?![\w’'])", corpus) is not None

    def claim(term: str, node: dict) -> None:
        term = term.strip()
        if len(term) < 2:
            return
        have = claimed.get(term)
        if have is None or _PRIORITY[node["kind"]] < _PRIORITY[have["kind"]]:
            claimed[term] = node

    def bare(title: str) -> str:
        return re.sub(r"^(The|A|An)\s+", "", title)

    for n in flat:
        kind, title = n["kind"], n["title"].replace("'", "’")
        named = any(c.startswith(PACK_ID + "/") for c in n.get("creatures", []))
        if kind == "chapter" or (kind == "location" and not title[:1].isdigit()):
            for t in {title, bare(title)}:
                if not lowercase_is_a_word(t) and len(t) > 3:
                    claim(t, n)
        elif kind in ("npc", "monster"):
            parts = [p.strip(" ,") for p in re.split(r"\s*/\s*|\s+and\s+|,\s*", title) if p.strip(" ,")] + [title]
            for t in dict.fromkeys(p for p in parts if p):
                if named or not lowercase_is_a_word(t):
                    claim(t, n)
                    claim(bare(t), n)
            if named:                                                      # first names too (Leanara, Ursic One-Ear -> Ursic)
                for t in parts:
                    words = t.split()
                    first = words[0] if words[0] not in ("The", "Master") else words[-1]
                    first = re.sub("’s$", "", first)
                    if len(first) > 3 and first[0].isupper() and first != "Ghost":
                        claim(first, n)

    # the names the book repeats, taken by a page that has them in its title or among the words of the title of a person
    counts: dict[str, int] = {}
    for m in _PROPER.finditer(corpus):
        p = m.group(0).strip()
        counts[p] = counts.get(p, 0) + 1
    lonely: list[str] = []
    for term, count in sorted(counts.items(), key=lambda kv: -kv[1]):
        if count < MIN_REPEATS or term in claimed or term.isupper() or len(term) < 2 or (term.endswith("’s") and term[:-2] in claimed):
            continue
        if lowercase_is_a_word(term) or term.split()[0] in ("Roll", "Random", "Rulebook", "Door", "NORTH", "SOUTH", "EAST", "WEST"):
            continue
        target = None
        if term in KEYWORD_TARGETS:
            kind, title = KEYWORD_TARGETS[term]
            target = next((n for n in flat if n["kind"] == kind and n["title"] == title), None)
        if target is None:
            options = [n for n in flat if n["kind"] != "text"
                       and (bare(n["title"]) == term or re.search(rf"(?<![\w’']){re.escape(term)}(?![\w’'])", n["title"].replace("'", "’")))]
            options.sort(key=lambda n: _PRIORITY[n["kind"]])
            target = options[0] if options else None
        if target is None:
            lonely.append(f"{term} ({count})")
        else:
            claim(term, target)
    for term, (kind, title) in KEYWORD_TARGETS.items():                    # chosen by hand: always
        target = next((n for n in flat if n["kind"] == kind and n["title"] == title), None)
        if target is not None:
            claimed[term] = target

    for n in flat:
        words = sorted((t for t, node in claimed.items() if node is n), key=lambda t: (-len(t), t))
        if words:
            n["keywords"] = words
    return len(claimed), lonely


# ------------------------------------------------------------------------------------------------ the pictures of the book
IMAGES = OUT / "images"
PICTURE_DIRS = ("chapters", "maps", "people", "art")          # what this script writes (and clears) inside images/
OVERVIEW_LEFT = {6: 7}                                         # page -> the page whose map it completes
MAX_WIDTH = {"maps": 1400, "chapters": 700, "people": 360, "art": 800}
CREAM = (251, 246, 230)                                       # what a map is flattened on: the pages of the app


def to_image(pix):
    from PIL import Image
    return Image.frombytes("RGBA" if pix.alpha else "RGB", (pix.width, pix.height), pix.samples)


def is_panel(im) -> bool:
    """The blank parchment boxes the text is printed on: light, yellowish and without detail (a portrait or a map has a lot of it)."""
    import statistics
    small = im.resize((32, 32)).crop((3, 3, 29, 29)).convert("RGB")
    px = list(small.getdata())
    lum = [0.3 * r + 0.59 * g + 0.11 * b for r, g, b in px]
    r, g, b = (sum(c[i] for c in px) / len(px) for i in range(3))
    spread = statistics.pstdev(lum)
    return (sum(lum) / len(lum) > 195 and spread < 20 and r - b > 25) or spread < 6          # or a flat colour, whatever it is


def load_picture(doc, xref: int, smask: int):
    pix = pymupdf.Pixmap(doc, xref)
    if pix.alpha == 0 and smask:
        pix = pymupdf.Pixmap(pix, pymupdf.Pixmap(doc, smask))
    if pix.n - pix.alpha > 3:                                    # CMYK etc.
        pix = pymupdf.Pixmap(pymupdf.csRGB, pix)
    return to_image(pix)


def save_picture(im, folder: str, name: str) -> str:
    from PIL import Image
    if im.width > MAX_WIDTH[folder]:
        im = im.resize((MAX_WIDTH[folder], round(im.height * MAX_WIDTH[folder] / im.width)), Image.LANCZOS)
    target = IMAGES / folder
    target.mkdir(parents=True, exist_ok=True)
    if im.mode == "RGBA" and folder not in ("maps", "chapters"):     # people and art keep their soft edges
        path = target / f"{name}.png"
        im.save(path, optimize=True)
    else:
        if im.mode == "RGBA":
            flat = Image.new("RGB", im.size, CREAM)
            flat.paste(im, mask=im.split()[-1])
            im = flat
        path = target / f"{name}.jpg"
        im.convert("RGB").save(path, quality=82, optimize=True)
    return f"images/{folder}/{path.name}"


def place_pictures(doc, flat: list[dict], geometry: dict[str, tuple[int, float, float]]) -> dict[str, int]:
    """Takes the pictures out of the PDF and gives each to a page of the adventure (`images:` on the node, files in images/).
    The blank parchment boxes and the page furniture (used on many pages) are left out. A chapter gets the picture that opens it, a
    map the plan of its place, a person the portrait printed next to its heading; the rest go to the heading nearest on the page.
    `geometry`: node id -> (page, x, y) of its heading."""
    for folder in PICTURE_DIRS:
        shutil.rmtree(IMAGES / folder, ignore_errors=True)
    used_on: dict[int, int] = {}
    for pno in range(len(doc)):
        for xref in {i[0] for i in doc[pno].get_images(full=True)}:
            used_on[xref] = used_on.get(xref, 0) + 1

    by_page: dict[int, list[dict]] = {}
    for n in flat:
        by_page.setdefault(n["page"], []).append(n)
    maps_on: dict[int, list[dict]] = {}
    for n in flat:
        if n["kind"] == "map":
            maps_on.setdefault(n["page"], []).append(n)
    # people whose heading the layout reader did not find: it is the name in capitals printed in the box under their portrait
    for n in flat:
        if n["kind"] in ("npc", "monster") and n["id"] not in geometry:
            wanted = [re.sub(r"\s+", " ", n["title"]).upper().replace("'", "’")]
            wanted += [p.strip().upper() for p in re.split(r"\s*/\s*|\s+and\s+", n["title"])]
            for block in doc[n["page"] - 1].get_text("blocks"):
                text = re.sub(r"\s+", " ", block[4]).strip().upper().replace("'", "’")
                if text in wanted:
                    geometry[n["id"]] = (n["page"], (block[0] + block[2]) / 2, block[1])
                    break
    names: dict[str, int] = {}
    stats = {"chapters": 0, "maps": 0, "people": 0, "art": 0}

    def attach(node: dict, folder: str, pix) -> None:
        base = slug(node["id"].replace("/", "-"))[:80]
        names[base] = names.get(base, 0) + 1
        name = base if names[base] == 1 else f"{base}-{names[base]}"
        node.setdefault("images", []).append(save_picture(pix, folder, name))
        stats[folder] += 1

    def nearest(page: int, cx: float, y_top: float, y_bottom: float, x0: float = 0, x1: float = 9999) -> dict:
        """The heading printed just below the picture (portraits sit above their box), else the nearest above it."""
        here = [(geometry[n["id"]], n) for n in by_page.get(page, []) if n["id"] in geometry and n["kind"] not in ("table", "text")]
        inside = [(g, n) for g, n in here if y_top - 10 <= g[2] <= y_bottom + 60 and x0 - 40 <= g[1] <= x1 + 40]    # a name printed over the picture
        if inside:
            return max(inside, key=lambda gn: gn[0][2])[1]
        below = [(g, n) for g, n in here if g[2] >= y_bottom - 40 and abs(g[1] - cx) < 220]
        if below:
            return min(below, key=lambda gn: (gn[0][2] - y_bottom, abs(gn[0][1] - cx)))[1]
        above = [(g, n) for g, n in here if g[2] <= y_top + 40]
        if above:
            return max(above, key=lambda gn: gn[0][2])[1]
        before = [n for n in flat if n["page"] <= page and n["kind"] not in ("table", "text")]       # no heading on that page: the page it belongs to
        return before[-1] if before else flat[0]

    for pno in range(1, len(doc) + 1):
        page = doc[pno - 1]
        items = []
        seen: set[tuple] = set()
        for info in page.get_image_info(xrefs=True):
            xref, w, h, bb = info["xref"], info["width"], info["height"], info["bbox"]
            bw, bh = bb[2] - bb[0], bb[3] - bb[1]
            key = (xref, round(bb[0]), round(bb[1]))
            if xref == 0 or key in seen or used_on.get(xref, 0) > 2 or w < 250 or h < 250 or bw < 60 or bh < 60 or (w, h) == (1296, 1683):
                continue
            seen.add(key)
            items.append({"xref": xref, "w": w, "h": h, "bb": bb})
        if not items:
            continue
        smasks = {i[0]: i[1] for i in page.get_images(full=True)}
        pictures = []
        for it in items:
            try:
                im = load_picture(doc, it["xref"], smasks.get(it["xref"], 0))
            except Exception:
                continue
            if is_panel(im):
                continue
            it["pix"] = im
            pictures.append(it)

        # the picture that opens a chapter: a strip of the map with the name of the place written on it
        for it in list(pictures):
            chapter = next((n for n in by_page.get(pno, []) if n["kind"] == "chapter" and n["page"] == pno), None)
            if chapter and it["w"] == 483 and it["h"] <= 330:
                attach(chapter, "chapters", it["pix"])
                pictures.remove(it)
        # the big map of the valley is printed over two pages: the left half goes with the map of the page after
        if pno in OVERVIEW_LEFT and maps_on.get(OVERVIEW_LEFT[pno]):
            half = max(pictures, key=lambda p: p["w"] * p["h"], default=None)
            if half is not None:
                attach(maps_on[OVERVIEW_LEFT[pno]][0], "maps", half["pix"])
                pictures.remove(half)
        # the plans: as many of the biggest pictures of the page as it has maps
        wanted = maps_on.get(pno, [])
        if wanted:
            big = sorted((p for p in pictures if min(p["w"], p["h"]) >= 480), key=lambda p: -p["w"] * p["h"])[:len(wanted)]
            big.sort(key=lambda p: (round(p["bb"][1] / 40), p["bb"][0]))
            for node, it in zip(sorted(wanted, key=lambda n: (geometry.get(n["id"], (0, 0, 0))[2], geometry.get(n["id"], (0, 0, 0))[1])), big):
                attach(node, "maps", it["pix"])
                pictures.remove(it)
        # the rest: people and other art, by the heading they sit next to
        for it in pictures:
            cx, y0, y1 = (it["bb"][0] + it["bb"][2]) / 2, it["bb"][1], it["bb"][3]
            node = nearest(pno, cx, y0, y1, it["bb"][0], it["bb"][2])
            folder = "people" if node["kind"] in ("npc", "monster") else "art"
            attach(node, folder, it["pix"])
    return stats


def give_portraits(flat: list[dict]) -> int:
    """The pack's own creatures (the named NPCs) show their portrait wherever the Reference shows them: `image:` in creatures.yaml,
    for those that have none yet. The picture is the one printed with them in the book."""
    path = OUT / "creatures.yaml"
    if not path.exists():
        return 0
    text = path.read_text(encoding="utf-8")
    given = 0
    for n in flat:
        pics = [i for i in n.get("images", []) if i.startswith("images/people/")]
        keys = [c for c in n.get("creatures", []) if c.startswith(PACK_ID + "/monster/")]
        if not pics or len(keys) != 1:
            continue
        cid = keys[0].split("/")[-1]
        block = re.search(rf"(?m)^  - id: {re.escape(cid)}\n(?:(?!  - id: ).*\n?)*", text)
        if not block or "\n    image: " in block.group(0):
            continue
        entry = block.group(0)
        fixed = re.sub(r"(?m)^(    category: .*)$", lambda m: m.group(1) + f"\n    image: {pics[0]}", entry, count=1)
        if fixed != entry:
            text = text.replace(entry, fixed, 1)
            given += 1
    path.write_text(text, encoding="utf-8")
    return given


# ------------------------------------------------------------------------------------------------ the tree
def main() -> int:
    src = next(s for s in build.SOURCES if s["key"] == SOURCE_KEY)
    src = dict(src, drop_caps=DROP_CAPS)
    ctx = build.prepare(src, 3)
    doc = ctx.doc
    creatures = core_creatures()
    warnings_out: list[str] = list(ctx.warnings)

    by_id = {s.id: s for s in ctx.sections}
    children: dict[int | None, list] = {}
    for s in ctx.sections:
        children.setdefault(s.parent_id, []).append(s)

    def chapter_of(s):
        while s.parent_id is not None:
            s = by_id[s.parent_id]
        return s

    # --- tables: the extractor's where it found rows, the page's text where it did not
    tables_out: list[dict] = []
    table_name: dict[int, str] = {}                 # section id -> name in tables.yaml
    used_names: set[str] = set()

    def unique(name: str) -> str:
        n, i = name, 2
        while n in used_names:
            n, i = f"{name} ({i})", i + 1
        used_names.add(n)
        return n

    good: dict[tuple[int, str], object] = {}
    for t in ctx.tables:
        if t.rows and not t.problems:
            good.setdefault((t.page, build.table_title(ctx, t)), t)

    for s in ctx.sections:
        if s.kind != "table":
            continue
        if ATTACK_TITLES.match(s.title):
            continue                                # the attacks of a creature: in Core with the creature
        chapter = chapter_of(s).title
        title = f"{chapter}: {s.title}" if s.title.lower() in GENERIC_TITLES else s.title
        name = unique(title)
        t = good.get((s.page_start, s.title))
        if t is not None and t.dice:
            rows = [{"roll": r.roll_text or str(i + 1), "cells": [ctx.fix(c) for c in r.cells]} for i, r in enumerate(t.rows)]
            tables_out.append({"name": name, "dice": t.dice, "columns": [ctx.fix(c) for c in t.columns], "page": s.page_start, "rows": rows})
        else:
            raw = raw_roll_table(doc, s.page_start)
            columns = ["EVENT"]
            if not raw:
                head, raw = block_table(doc, s.page_start, s.title)
                columns = [re.sub(r"^D\d+(/D\d+)?\s+", "", h) for h in head] or ["EFFECT"]
            if not raw:
                warnings_out.append(f"table '{s.title}' p{s.page_start}: no rows found (left out)")
                used_names.discard(name)
                continue
            last = raw[-1][0]
            top = int(re.split(r"–", last)[-1]) if re.match(r"^\d", last) else 0
            dice = {4: "D4", 6: "D6", 8: "D8", 10: "D10", 12: "D12", 20: "D20"}.get(top)
            if dice is None and top:
                warnings_out.append(f"table '{s.title}' p{s.page_start}: rolls go up to {top}, which is no die (written without dice)")
            if s.title == "Demonic Alterations":
                dice = None                          # the book says D6 or D10 depending on how far the character got lost
            tables_out.append({"name": name, **({"dice": dice} if dice else {}), "columns": columns, "page": s.page_start,
                               "rows": [{"roll": roll, "cells": [text]} for roll, text in raw]})
        table_name[s.id] = name

    # --- the tree
    used_ids: set[str] = set()

    def node_id(parent: str, title: str) -> str:
        base = f"{parent}/{slug(title)}" if parent else slug(title)
        n, i = base, 2
        while n in used_ids:
            n, i = f"{base}-{i}", i + 1
        used_ids.add(n)
        return n

    unmatched: list[str] = []
    geometry: dict[str, tuple[int, float, float]] = {}      # node id -> (page, x, y) of its heading, to put the pictures next to it

    def build_node(s, parent_id: str) -> dict | None:
        if s.kind in EMPTY_KINDS:
            return None
        if s.kind == "table" and s.id not in table_name:
            return None
        title = s.title.strip()
        node: dict = {}
        kind = s.kind
        number = None
        m = re.match(r"^(\d+[a-z]?)\.\s+(.*)$", title)
        if m and kind == "section":
            kind, number, title = "location", m.group(1), m.group(2)
        nid = node_id(parent_id, f"{number}-{title}" if number else title)
        node["id"] = nid
        node["title"] = title
        if s.anchor is not None:
            geometry[nid] = (s.anchor.page, (s.anchor.x0 + s.anchor.x1) / 2, s.anchor.y0)
        if s.kind == "chapter" and s.chapter_no:
            node["number"] = s.chapter_no
        if number:
            node["number"] = number
        node["kind"] = kind
        node["page"] = s.page_start
        if s.page_end and s.page_end != s.page_start:
            node["page_end"] = s.page_end
        body = strip_labels(tidy(build.section_body(ctx, s)))
        for start, letter in TEXT_FIXES.items():
            if body.startswith(start):
                body = letter + body
        place_text = ""
        linked: list[str] = []
        if kind in ("npc", "monster"):
            body, place_text = split_stats(body)
        else:
            body, linked, missed = inline_stats(body, creatures, STRAY_BLOCKS.get((title, s.page_start), ""))
            unmatched.extend(f"stat block next to '{c}' in '{title}' p{s.page_start}" for c in missed)
        if body and kind not in ("table", "map"):                      # a map's "text" is only the numbers of its key
            node["body"] = body
        if kind in ("npc", "monster"):
            names = [n.strip() for n in re.split(r"\s*/\s*|\s+and\s+", title)] if "/" in title else [title]
            keys = [creatures[norm(n)] for n in names + [title] if norm(n) in creatures]
            keys = list(dict.fromkeys(keys))
            if keys:
                node["creatures"] = keys
            else:
                unmatched.append(f"{kind} '{title}' p{s.page_start}")
        if linked:
            node["creatures"] = linked
        if kind == "table":
            node["table"] = table_name[s.id]
        kids = build_children(children.get(s.id, []), nid)
        if kind in ("npc", "monster"):
            # what follows a creature's attack table is the place's own text again (the exits...): it goes after the creature, not inside it
            after = [k for k in kids if k["kind"] == "section" and k["title"] == "Monster Attacks"]
            kids = [k for k in kids if k not in after]
            for k in after:
                k["kind"], k["title"] = "text", "Continued"
                k["id"] = re.sub(r"/monster-attacks(-" + chr(92) + "d+)?$", "/continued" + chr(92) + "1", k["id"])
            if place_text:
                after.insert(0, {"id": "", "title": "Continued", "kind": "text", "page": s.page_end or s.page_start, "body": place_text})
            node["_hoist"] = after
        if kids:
            node["sections"] = kids
        if not node.get("body") and not kids and not node.get("_hoist") and not node.get("creatures") and kind not in ("table", "map"):
            return None
        return node

    def build_children(sections, parent_id: str) -> list[dict]:
        out: list[dict] = []
        for c in sections:
            if parent_id == "" and c.title in SKIP_CHAPTERS:
                continue
            if parent_id == "":
                creatures.chapter = c.title
            n = build_node(c, parent_id)
            if n:
                hoisted = n.pop("_hoist", [])
                for h in hoisted:
                    h["id"] = node_id(parent_id, "Continued")
                out.extend([n, *hoisted])
        return out

    chapters = build_children(children.get(None, []), "")

    # A text that begins in lower case continues the one before it (the page turned between them): it goes back where it belongs.
    flat: list[dict] = []

    def walk(nodes):
        for n in nodes:
            flat.append(n)
            walk(n.get("sections", []))

    walk(chapters)
    moved = 0
    for i, n in enumerate(flat):
        b = n.get("body", "")
        if n["kind"] in ("table", "map") or not b[:1].islower():
            continue
        target = next((p for p in reversed(flat[:i]) if p.get("body") and not ends_sentence(p["body"]) and p["kind"] not in ("table", "map")), None)
        if target is None:
            continue
        first, _, rest = b.partition("\n")
        target["body"] = f"{target['body']} {first}"
        if rest.strip():
            n["body"] = rest
        else:
            del n["body"]
        moved += 1
    if moved:
        print(f"{moved} texts that began in the middle of a sentence were put back after the one they continue")

    walk(chapters) if False else None
    flat.clear()
    walk(chapters)
    # the name printed in capitals in the box of a person is a caption: its page has the name already
    captions = {n["title"].replace("'", "’").upper() for n in flat if n["kind"] in ("npc", "monster")}
    for n in flat:
        if n.get("body") and n["kind"] not in ("npc", "monster"):
            kept = [l for l in n["body"].split("\n") if l.strip().replace("'", "’") not in captions]
            if len(kept) != len(n["body"].split("\n")):
                n["body"] = "\n".join(kept).strip()
                if not n["body"]:
                    del n["body"]
    linked, lonely = assign_keywords(flat)
    print(f"{linked} words lead to a page; names the book repeats that no page takes: {', '.join(lonely) or 'none'}")
    pictures = place_pictures(doc, flat, geometry)
    print("pictures:", ", ".join(f"{n} {k}" for k, n in pictures.items()))
    print(f"{give_portraits(flat)} named NPCs got their portrait in creatures.yaml")

    manifest = {
        "id": PACK_ID,
        "name": "Dragonbane: The Misty Vale",
        "version": "1.0.0",
        "author": "Free League",
        "description": "The Misty Vale adventure, parsed from the PDF: its chapters, locations, NPCs, monsters, sidebars, maps and tables.",
        "sources": [{
            "key": SOURCE_KEY, "title": src["title"], "short": "Adventure", "file": src["file"], "pages": len(doc),
            "page_offset": src["page_offset"], "first_numbered_page": 5, "unnumbered_pages": [6, 7, 8],
        }],
    }
    write_yaml(OUT / "manifest.yaml", manifest)
    write_yaml(OUT / "adventure.yaml", {"title": "The Misty Vale", "source": SOURCE_KEY, "chapters": chapters})
    write_yaml(OUT / "tables.yaml", {"source": SOURCE_KEY, "tables": tables_out})

    count = lambda nodes, k=None: sum((1 if k is None or n["kind"] == k else 0) + count(n.get("sections", []), k) for n in nodes)  # noqa: E731
    print(f"chapters: {len(chapters)}  nodes: {count(chapters)}  locations: {count(chapters, 'location')}  "
          f"npcs: {count(chapters, 'npc')}  monsters: {count(chapters, 'monster')}  sidebars: {count(chapters, 'sidebar')}  "
          f"maps: {count(chapters, 'map')}  tables: {len(tables_out)}")
    for w in warnings_out:
        print("warning:", w)
    for u in unmatched:
        print("no creature in Core for", u)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
