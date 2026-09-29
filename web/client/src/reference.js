// Shared reference state for the GM sidebar and the player Rules tab: one merged, alphabetical index of
// content types + rules chapters. An index entry can have several tabs:
//  - a chapter titled like a content type becomes that entry's "General info" tab;
//  - GROUPS folds other content types into one entry (Gear holds Weapons and Armor).
import { computed, reactive, ref } from 'vue';
import { apiGet } from './api.js';

const sameLabel = (a, b) => a.trim().toLowerCase() === b.trim().toLowerCase();

// host content type -> [{id of a content type, tab label}]; the host's own tab comes first
const GROUPS = {
  gear: { first: 'General', others: [{ id: 'weapons', label: 'Weapons' }, { id: 'armor', label: 'Armor' }] },
};

export function useReference(token, gmPrefix = '') {
  const contentTypes = ref([]); // [{id, label, count}]
  const rulesChapters = ref([]); // [{key, title, introOnly?}]
  const current = ref(null); // {mode, type, label, tabs[], tab (index), entryKey}

  const items = computed(() => {
    const typeById = new Map(contentTypes.value.map((t) => [t.id, t]));
    const absorbed = new Set();
    const merged = new Set();
    const list = contentTypes.value.map((t) => {
      const group = GROUPS[t.id];
      const ch = rulesChapters.value.find((r) => sameLabel(r.title, t.label));
      let tabs = [];
      let count = t.count;
      if (group) {
        const parts = group.others.filter((o) => typeById.has(o.id));
        if (parts.length) {
          tabs = [
            { label: group.first, mode: 'tables', type: t.id, count: t.count },
            ...parts.map((o) => ({ label: o.label, mode: 'tables', type: o.id, count: typeById.get(o.id).count })),
          ];
          parts.forEach((o) => absorbed.add(o.id));
          count = tabs.reduce((n, x) => n + x.count, 0);
        }
      } else if (ch) {
        merged.add(ch.key);
        tabs = [
          { label: `All ${t.label.toLowerCase()}`, mode: 'tables', type: t.id, count: t.count },
          { label: 'General info', mode: 'rules', type: ch.key },
        ];
      }
      return { mode: 'tables', type: t.id, label: t.label, count, tabs };
    }).filter((i) => !absorbed.has(i.type));
    for (const r of rulesChapters.value) {
      if (!merged.has(r.key)) list.push({ mode: 'rules', type: r.key, label: r.title, count: null, tabs: [] });
    }
    return list.sort((a, b) => a.label.localeCompare(b.label));
  });

  // Must stay referentially stable across re-renders: RulesView reloads whenever jumpTo changes identity
  const jump = computed(() => {
    const r = current.value;
    if (!r) return null;
    const tab = r.tabs[r.tab];
    return { type: tab ? tab.type : r.type, key: r.entryKey ?? null };
  });

  async function load() {
    const data = await apiGet(`${gmPrefix}/content`, typeof token === 'function' ? token() : token);
    contentTypes.value = data.types ?? [];
    rulesChapters.value = data.rules ?? [];
  }

  function select(item, entryKey = null, tab = 0) {
    current.value = { mode: item.mode, type: item.type, label: item.label, tabs: item.tabs ?? [], tab, entryKey };
  }

  // Open whatever a link points at: {key, type?} — a chapter, or an entry of a content type
  function goTo(target) {
    const key = target?.key ?? '';
    const hostOf = (mode, type) => {
      for (const item of items.value) {
        const tab = item.tabs.findIndex((t) => t.mode === mode && t.type === type);
        if (tab >= 0) return { item, tab };
        if (!item.tabs.length && item.mode === mode && item.type === type) return { item, tab: 0 };
      }
      return null;
    };
    if (rulesChapters.value.some((r) => r.key === key)) {
      const found = hostOf('rules', key);
      if (found) select(found.item, null, found.tab);
      return;
    }
    const typeId = target?.type || key;
    const found = hostOf('tables', typeId);
    if (found) select(found.item, target?.type ? key : null, found.tab);
  }

  return reactive({ contentTypes, rulesChapters, current, items, jump, load, select, goTo });
}
