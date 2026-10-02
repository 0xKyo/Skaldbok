// One search over everything the Reference holds: the entries of every content type (the server searches those), the rules chapters (read once, and
// searched here) and the creatures. The answer is a flat list of what matched, each one with the page it belongs to, ready for state.goTo().
import { apiGet } from '../api.js';

// What of a rules entry can be searched: its title, text, sections and tables (also used to filter a chapter's own list)
export const ruleHaystack = (e) => [
  e.title, e.name, e.body,
  ...(e.sections ?? []).flatMap((s) => [s.title, s.body]),
  ...(e.tables ?? []).flatMap((t) => [t.title, ...(t.columns ?? []), ...t.rows.flatMap((r) => r.cells)]),
].filter(Boolean).join('\n').toLowerCase();

const PER_PAGE = 8;       // the most that one type or chapter adds to the list
const MAX = 80;

export function createGlobalSearch(token, gmPrefix = '') {
  const chapters = new Map();          // chapter key -> its entries (read once)
  const tokenOf = () => (typeof token === 'function' ? token() : token);

  async function chapterEntries(ch) {
    if (chapters.has(ch.key)) return chapters.get(ch.key);
    let entries = [];
    if (ch.introOnly) {
      const res = await apiGet(`${gmPrefix}/content/${ch.key}`, tokenOf());
      const intro = res.intro;
      if (intro) entries = [{ key: '', title: ch.title, body: intro.body, sections: intro.sections ?? [], tables: intro.tables ?? [] }];
    } else {
      const res = await apiGet(`${gmPrefix}/rules/${ch.key}`, tokenOf());
      entries = res.rules ?? [];
      if (res.intro) entries = [{ key: '', title: ch.title, body: res.intro.body, sections: res.intro.sections ?? [], tables: res.intro.tables ?? [] }, ...entries];
    }
    chapters.set(ch.key, entries);
    return entries;
  }

  // The page (the label of the index entry) that holds a type or a chapter
  function categoryOf(items, mode, type) {
    for (const item of items) {
      if (item.tabs.some((t) => t.mode === mode && t.type === type) || (item.mode === mode && item.type === type)) return item.label;
    }
    return '';
  }

  async function run(query, state) {
    const text = query.trim();
    const q = text.toLowerCase();
    if (!q) return [];
    const found = [];
    const jobs = [];
    for (const t of state.contentTypes) {
      jobs.push((async () => {
        const url = t.id === 'creatures' ? `${gmPrefix}/creatures?q=${encodeURIComponent(text)}` : `${gmPrefix}/content/${t.id}?q=${encodeURIComponent(text)}`;
        const res = await apiGet(url, tokenOf());
        const list = t.id === 'creatures' ? (res.creatures ?? []) : (res.entries ?? []);
        for (const e of list.slice(0, PER_PAGE)) {
          found.push({ mode: 'tables', type: t.id, key: e.key, title: e.name ?? e.title ?? e.key, note: e.sub ?? e.subtitle ?? '', hit: (e.name ?? '').toLowerCase().includes(q) });
        }
      })());
    }
    for (const ch of state.rulesChapters) {
      jobs.push((async () => {
        const entries = await chapterEntries(ch);
        for (const e of entries.filter((x) => ruleHaystack(x).includes(q)).slice(0, PER_PAGE)) {
          found.push({ mode: 'rules', type: ch.key, key: e.key, title: e.title ?? e.name ?? ch.title, note: '', hit: (e.title ?? '').toLowerCase().includes(q) });
        }
      })());
    }
    await Promise.allSettled(jobs);                        // a type that cannot be read is just not in the list
    const items = state.items ?? [];
    return found
      .map((r) => ({ ...r, category: categoryOf(items, r.mode, r.type) || r.type }))
      .sort((a, b) => Number(b.hit) - Number(a.hit) || a.category.localeCompare(b.category) || a.title.localeCompare(b.title))
      .slice(0, MAX);
  }

  return { run };
}
