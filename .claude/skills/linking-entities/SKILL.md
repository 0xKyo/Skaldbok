---
name: linking-entities
description: How Skaldbok points from one thing to another (a spell on a sheet, an innate ability on a kin page, a skill in a requirement). Two patterns, INCLUDE (the entity is shown right there and can be opened and closed) and LINK (only its name, which goes to its own page). Use it whenever a page, a text or a data file mentions another entity.
---

# Linking entities: Include and Link

An *entity* is a card of the Reference (spell, ability, skill, kin, profession, weapon, armor, gear) or a rule. Whatever mentions one picks **one of two patterns**. Never invent a third (no plain text copies of another entity's words, no ad-hoc popups).

## 1. INCLUDE: show it here, open and close it

Use it when the reader should see *what the thing is* without leaving the page: a kin's innate ability, a spell on the character sheet.

- **In a text** (a kin, a rule, an intro): a line that is only `{{include: ref}}`. `ref` is the entity's id (`ability_adaptive`), its full key (`core/ability/adaptive`) or its name. The text file does **not** copy anything of the entity.
- **The server** (`web_views.cpp`: `includesShown`) sends the card with the entry as `includes: [{ref, key, kind, name, subtitle, fields[], body}]`; `ContentStore::resolveCard(ref)` finds it. A page that includes things by rule (the Kin page includes each `innate_abilities`) appends the `{{include: key}}` line in `ContentStore::resolveLinks`; the yaml says nothing.
- **The client:** `lib/ruletables.js` (`textBlocks(text, tables, includes)` turns the marker into a `{card}` block), drawn by `IncludedCard.vue`, which opens and closes (`startOpen`, `closable`) and whose name leads to the entity (`follow` with its key). `TextBlocks.vue` / `IntroBlocks.vue` do all of that for any text: use them, do not render blocks by hand.
- **Outside a text** (a list of entities on a page, as the spells of a sheet): `InlineCard.vue`, a line that opens into the card **without leaving the page**; it fetches the card once (`lib/cards.js`) and shows it with `IncludedCard`. Closed at first.

## 2. LINK: only the name, which goes to the page

Use it for a short mention: a requirement (`Knives 12`), a prerequisite, the skills of a profession, the banes of an armor.

- **In a text:** `[[name|key]]` (`key` = the English name or the full key) or `{{key: term}}` for a keyword. `RuleText.vue` draws it and emits `follow` with the key.
- **In a field of a card** the server writes the markup itself in `ContentStore::resolveLinks` (`link(...)`): `[[Knives|core/skill/knives]] 12`. Fields are drawn with `RuleText`, never with `{{ value }}`.
- **Going there:** `follow` -> `state.goTo({ key })` (`reference.js`) opens the page of the entity's kind, with it open; a full key is enough (`core/spell/fireball` finds `spells`). Where there is no navigation (pins, the creator) the link is only drawn.

## Rules for both

1. **Name entities by id in the data**, never by their words: `requirement: skills_knives` + `rank: 12`, `innate_abilities: [ability_adaptive]`, `heroic_abilities`, `banes`, `damage_types`, `school`, `prerequisite`, an action's `group`. The words come from the entity's own name, so they follow the language. A plain name (a homebrew's) still works. See docs/HOMEBREW.md.
2. **Ids are `<what>_<camelName>`** (`skills_bluffing`, `spell_cookFood`); the card key (`core/skill/myths-and-legends`) is derived from the id and never changes.
3. **Resolve after everything is loaded** (`resolveLinks`): a spell can name a spell that loads later. Store the raw reference in a prop (`requirement.ref`) at load and build the field there.
4. **Choose by the reader's need:** the thing itself matters here -> INCLUDE; the name is enough -> LINK. If the same entity is already included on the page, link to it elsewhere on the same page instead of including it twice.
5. **Test both ends:** server (`tests/content_test.cpp`: the field text / the `{{include: ...}}` line; `resolveCard` by key, id and name) and client (`IncludedCard.spec.js`, `InlineCard.spec.js`, `ruletables.spec.js`).
6. Update docs/HOMEBREW.md (data) and docs/WEB.md (API/page) when a pattern gains a place to be used.
