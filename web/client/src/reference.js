// Shared reference state for the GM sidebar and the player Rules tab: one merged, alphabetical index of
// content types + rules chapters. An index entry can have several tabs:
//  - a chapter titled like a content type becomes that entry's "General info" tab;
//  - GROUPS folds other content types into one entry (Gear holds Weapons and Armor).
import { computed, reactive, ref } from 'vue';
import { apiGet } from './api.js';
import { locale, t } from './i18n.js';

const sameLabel = (a, b) => a.trim().toLowerCase() === b.trim().toLowerCase();

// host content type -> [{id of a content type, tab label}]; the host's own tab comes first (the labels are put in the page's language)
const GROUPS = {
  gear: { first: 'General', others: [{ id: 'weapons', label: 'Weapons' }, { id: 'armor', label: 'Armor' }] },
};

export function useReference(token, gmPrefix = '') {
  const contentTypes = ref([]); // [{id, label, count}]
  const rulesChapters = ref([]); // [{key, title, introOnly?}]
  const query = ref(''); // what the search bar asks for
  const current = ref(null); // {mode, type, label, tabs[], tab (index), entryKey}

  const items = computed(() => {
    const typeById = new Map(contentTypes.value.map((t) => [t.id, t]));
    const absorbed = new Set();
    const merged = new Set();
    const list = contentTypes.value.map((type) => {
      const group = GROUPS[type.id];
      // the general text of a type is an intro-only chapter with the type's id as key ("Magic" for spells), or a chapter titled like it
      const ch = rulesChapters.value.find((r) => r.introOnly && r.key === type.id) ?? rulesChapters.value.find((r) => sameLabel(r.title, type.label));
      let tabs = [];
      let count = type.count;
      if (group) {
        const parts = group.others.filter((o) => typeById.has(o.id));
        if (parts.length) {
          tabs = [
            { label: t(group.first), mode: 'tables', type: type.id, count: type.count },
            ...parts.map((o) => ({ label: t(o.label), mode: 'tables', type: o.id, count: typeById.get(o.id).count })),
          ];
          parts.forEach((o) => absorbed.add(o.id));
          count = tabs.reduce((n, x) => n + x.count, 0);
          if (ch) {                                       // the general text of the page is one more tab, not a block above the list
            merged.add(ch.key);
            tabs.push({ label: t('General Info'), mode: 'rules', type: ch.key });
          }
        }
      } else if (ch) {
        merged.add(ch.key);
        tabs = [
          { label: ch.title === type.label ? t('All {what}', { what: type.label.toLowerCase() }) : type.label, mode: 'tables', type: type.id, count: type.count },
          { label: t('General Info'), mode: 'rules', type: ch.key },
        ];
      }
      return { mode: 'tables', type: type.id, label: !group && ch ? ch.title : type.label, count, tabs };
    }).filter((i) => !absorbed.has(i.type));
    for (const r of rulesChapters.value) {
      if (!merged.has(r.key)) list.push({ mode: 'rules', type: r.key, label: r.title, count: null, tabs: [] });
    }
    return list.sort((a, b) => a.label.localeCompare(b.label, locale()));
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
    current.value = { mode: item.mode, type: item.type, label: item.label, tabs: item.tabs ?? [], tab, entryKey, seen: entryKey };
  }

  // the entry the list has open now (the list picks it by itself): kept so that going back to this page opens the same one
  function seen(key) {
    if (current.value && (current.value.seen ?? null) !== (key ?? null)) current.value.seen = key ?? null;
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
      if (found) select(found.item, target?.entry || null, found.tab);
      return;
    }
    // a full key of a card ("core/ability/adaptive"): the page of its kind, open on it
    const kindPage = { skill: 'skills', ability: 'abilities', spell: 'spells', kin: 'kin', profession: 'professions', weapon: 'weapons', armor: 'armor', gear: 'gear' };
    const parts = key.split('/');
    const typeId = target?.type || (parts.length === 3 && kindPage[parts[1]]) || key;
    if (typeId !== key && !target?.type) target = { ...target, type: typeId };
    const found = hostOf('tables', typeId);
    if (found) {
      select(found.item, target?.type ? key : null, found.tab);
      return;
    }
    // a link to a section of a page's intro ([[Encumbrance]]): open that page, its intro is always shown
    const owner = contentTypes.value.find((t) => (t.sections ?? []).some((s) => sameLabel(s, key)));
    const home = owner && hostOf('tables', owner.id);
    if (home) {
      const info = home.item.tabs.findIndex((t) => t.mode === 'rules');
      select(home.item, null, info >= 0 ? info : home.tab);
    }
  }

  return reactive({ contentTypes, rulesChapters, query, current, items, jump, load, select, goTo, seen });
}
