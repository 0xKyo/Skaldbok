// Tables inside rule text: a line that is only "{{table: Name}}" is where that table goes.
const MARKER = /^\{\{table:\s*(.+?)\s*\}\}$/i;

export const markerName = (line) => line.trim().match(MARKER)?.[1] ?? null;

const same = (a, b) => a.trim().toLowerCase() === b.trim().toLowerCase();

// A text as blocks in reading order: {text} paragraphs and {table} at each marker (a marker with no such table is dropped).
export function textBlocks(text, tables = []) {
  const out = [];
  for (const line of (text ?? '').split('\n')) {
    if (!line.trim()) continue;
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
