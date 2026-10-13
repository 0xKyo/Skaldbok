// Tables inside rule text: a line that is only "{{table: Name}}" is where that table goes.
const MARKER = /^\{\{table:\s*(.+?)\s*\}\}$/i;
const INCLUDE = /^\{\{include:\s*(.+?)\s*\}\}$/i;
// A bullet line: "* text", "- text", "• text" or "✦text".
const BULLET = /^\s*(?:[*•-]\s+|✦\s*)(.*\S.*)$/;

export const markerName = (line) => line.trim().match(MARKER)?.[1] ?? null;

const same = (a, b) => a.trim().toLowerCase() === b.trim().toLowerCase();

// A text as blocks in reading order: {text} paragraphs, {list} for a run of bullet lines, {table} at each table marker and {card} at each
// include marker (a marker with nothing to show is dropped).
export function textBlocks(text, tables = [], includes = []) {
  const out = [];
  let list = null;
  for (const line of (text ?? '').split('\n')) {
    if (!line.trim()) continue;
    const ref = line.trim().match(INCLUDE)?.[1];           // "{{include: Ref}}": another card, drawn right here
    if (ref !== undefined) {
      list = null;
      const card = includes.find((c) => same(c.ref, ref));
      if (card) out.push({ card });
      continue;
    }
    const bullet = line.match(BULLET);
    if (bullet) {
      if (!list) { list = []; out.push({ list }); }
      list.push(bullet[1]);
      continue;
    }
    list = null;
    const name = markerName(line);
    if (name === null) { out.push({ text: line }); continue; }
    const table = tables.find((t) => same(t.title, name));
    if (table) out.push({ table });
  }
  return out;
}

// The tables a text places with a marker.
export const tablesMarkedIn = (text, tables = []) => tables.filter((t) => !unplacedTables([text], [t]).length);

// The tables an entry has that none of its texts places with a marker: shown after the text.
export function unplacedTables(texts, tables = []) {
  const placed = new Set();
  for (const text of texts) for (const line of (text ?? '').split('\n')) {
    const name = markerName(line);
    if (name !== null) placed.add(name.toLowerCase());
  }
  return tables.filter((t) => !placed.has(t.title.trim().toLowerCase()));
}
