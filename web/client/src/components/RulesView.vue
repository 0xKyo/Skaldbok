<script setup>
import { computed, onMounted, ref, watch } from 'vue';
import { apiGet } from '../api.js';
import { locale, t } from '../i18n.js';
import DataTable from './DataTable.vue';
import NpcCreator from './NpcCreator.vue';
import PinButton from './PinButton.vue';
import RuleText from './RuleText.vue';
import IntroBlocks from './IntroBlocks.vue';
import TextBlocks from './TextBlocks.vue';
import { tablesMarkedIn, unplacedTables } from '../lib/ruletables.js';

const props = defineProps({
  token: { type: String, required: true },
  jumpTo: { type: Object, default: null }, // {key, type}
  mode: { type: String, default: 'tables' }, // 'tables' | 'rules'
  gmPrefix: { type: String, default: '' },   // '/gm' when used in GM view, '' for player
  hideTabs: { type: Boolean, default: false }, // GM sidebar already picks the category
});
const emit = defineEmits(['follow-key', 'seen']);

const summary = ref(null);
const type = ref(props.jumpTo?.type ?? (props.mode === 'rules' ? '' : 'spells'));
const entries = ref([]);
const intro = ref(null);      // {body, sections} — for inline-intro types
const loading = ref(false);
const error = ref('');
const selected = ref(null);
let pendingKey = props.jumpTo?.key ?? null;
let request = 0;

const showDetail = computed(() => selected.value !== null);

// what the 📌 button puts on the master's board: the entry open now, as it is on screen
function pinSelected() {
  const e = selected.value;
  if (!e) return null;
  const title = e.step ? `${e.step}. ${e.name ?? e.title}` : (e.name ?? e.title);
  return {
    type: e.fields?.length ? 'card' : 'rule',
    key: e.key,
    title,
    data: { subtitle: e.subtitle ?? '', fields: e.fields ?? [], body: e.body ?? '', sections: e.sections ?? [], tables: e.tables ?? [], includes: e.includes ?? [] },
  };
}
watch(selected, (e) => emit('seen', e?.key ?? null));       // the page you are on, for going back to it

// Side by side (wide screens) the list always has something open beside it: the first entry. On a phone the list and the entry take turns,
// so nothing is opened by itself: the list is what you see first.
const sideBySide = () => (typeof window.matchMedia === 'function' ? window.matchMedia('(min-width: 640px)').matches : true);
const firstOf = (list) => (sideBySide() ? (list[0] ?? null) : null);

// The rule chapter object for the currently selected type (null in Tables mode — only active in Rules mode)
const currentChapter = computed(() =>
  props.mode === 'rules'
    ? (summary.value?.rules?.find((r) => r.key === type.value) ?? null)
    : null,
);
const isRulesChapter = computed(() => currentChapter.value !== null);
// An intro-only chapter surfaces a content type's intro as a rules chapter
const isIntroOnly = computed(() => currentChapter.value?.introOnly === true);

// In Tables mode a type's intro is always shown above the list, except spells, skills and gear: theirs lives in the "General Info" tab.
const rulesIntroTypes = ['spells', 'skills', 'gear'];
const showInlineIntro = computed(() =>
  intro.value !== null && (props.mode === 'rules' ? !isIntroOnly.value : !rulesIntroTypes.includes(type.value)),
);

const paragraphs = computed(() =>
  selected.value ? (selected.value.body ?? '').split('\n').filter((p) => p.trim() !== '') : [],
);
const selectedSections = computed(() => selected.value?.sections ?? []);

const byTitle = (a, b) => (a.title ?? '').localeCompare(b.title ?? '', locale(), { sensitivity: 'base' });
const shownEntries = computed(() => {
  const tabList = grouped.value ? entries.value.filter((e) => e.tab === groupNames.value[groupTab.value]) : entries.value;
  return grouped.value ? [...tabList].sort(byTitle) : tabList;      // (the entries of a category and of a chapter are already in their order: see load)
});

// Chapters with `layout: groups` (World): each rule says which `tab` it belongs to; a tab lists its rules with the text of the chosen one beside them.
const grouped = computed(() => props.mode === 'rules' && layout.value === 'groups' && entries.value.some((e) => e.tab));
const groupNames = computed(() => [...new Set(entries.value.map((e) => e.tab).filter(Boolean))]);
const groupTab = ref(0);
watch(groupTab, () => {
  if (!shownEntries.value.includes(selected.value)) selected.value = firstOf(shownEntries.value);   // a new tab shows its first rule (not on a phone)
});

// Chapters with `layout: tabs` (Actions): every entry is a tab, and the rows of its table are the list, with each row's info beside it.
const layout = ref('');
const tabbed = computed(() => props.mode === 'rules' && layout.value === 'tabs' && entries.value.length > 0);
const tab = ref(0);
const row = ref(null);
const activeEntry = computed(() => (tabbed.value ? entries.value[tab.value] ?? null : null));
const activeTable = computed(() => activeEntry.value?.tables?.[0] ?? null);
// a tab of plain text (General Info): it has no table, or it has named sections (its tables are placed inside its text)
const textTab = computed(() => !!activeEntry.value && (!activeTable.value || (activeEntry.value.sections ?? []).length > 0));
const shownRows = computed(() =>
  [...(activeTable.value?.rows ?? [])].sort((a, b) => a.cells[0].localeCompare(b.cells[0], locale(), { sensitivity: 'base' })));   // A to Z
const paragraphsOf = (text) => (text ?? '').split('\n').filter((l) => l.trim());
watch(shownRows, () => {
  if (!tabbed.value) return;
  if (!shownRows.value.includes(row.value)) row.value = shownRows.value[0] ?? null;
}, { immediate: true });

async function load() {
  selected.value = null;
  intro.value = null;
  layout.value = '';
  groupTab.value = 0;
  tab.value = 0;
  row.value = null;
  if (!type.value) return;
  const mine = ++request;
  loading.value = true;
  try {
    let res;
    if (isIntroOnly.value) {
      // Fetch the content type's data and use its intro as the entry list
      res = await apiGet(`${props.gmPrefix}/content/${type.value}`, props.token);
      const introData = res.intro;
      const sects = [];
      const all = introData?.tables ?? [];
      const unplaced = unplacedTables([introData?.body, ...(introData?.sections ?? []).map((s) => s.body)], all);
      if (introData?.body) sects.push({ key: '__body__', title: currentChapter.value.title, body: introData.body, sections: [], tables: [...tablesMarkedIn(introData.body, all), ...unplaced] });
      for (let i = 0; i < (introData?.sections?.length ?? 0); i++) {
        const s = introData.sections[i];
        sects.push({ key: `__sec__${i}`, title: s.title, canon: s.canon, body: s.body, sections: [], tables: tablesMarkedIn(s.body, all) });
      }
      entries.value = sects;
      if (entries.value.length) {
        selected.value = pendingKey
          ? (entries.value.find((e) => e.key === pendingKey || e.title?.toLowerCase() === pendingKey || e.canon?.toLowerCase() === pendingKey) ?? firstOf(entries.value))
          : firstOf(entries.value);
        pendingKey = null;
      }
    } else if (isRulesChapter.value) {
      res = await apiGet(`${props.gmPrefix}/rules/${type.value}`, props.token);
      // A to Z, unless the order carries meaning: numbered steps, the tabs of a layout: tabs chapter; in a groups chapter each tab is sorted
      const raw = res.rules ?? [];
      entries.value = res.layout || raw.some((e) => e.step) ? raw : [...raw].sort(byTitle);
      intro.value = res.intro ?? null;
      layout.value = res.layout ?? '';
      if (entries.value.length) {
        selected.value = pendingKey
          ? (entries.value.find((e) => e.key === pendingKey || e.title?.toLowerCase() === pendingKey || e.canon?.toLowerCase() === pendingKey) ?? firstOf(entries.value))
          : firstOf(entries.value);
        pendingKey = null;
      }
    } else {
      res = await apiGet(`${props.gmPrefix}/content/${type.value}`, props.token);
      // A to Z, ignoring case; a search keeps the server's order (best match first)
      entries.value = [...res.entries].sort((a, b) => (a.name ?? '').localeCompare(b.name ?? '', locale(), { sensitivity: 'base' }));
      intro.value = res.intro ?? null;
      if (entries.value.length) {
        selected.value = pendingKey
          ? (entries.value.find((e) => e.key === pendingKey) ?? firstOf(entries.value))
          : firstOf(entries.value);
        pendingKey = null;
      }
    }
    if (grouped.value && selected.value) groupTab.value = Math.max(0, groupNames.value.indexOf(selected.value.tab));   // open on the tab of the wanted rule
    error.value = '';
  } catch (e) {
    if (mine === request) error.value = e.message;
  } finally {
    if (mine === request) loading.value = false;
  }
}

// Follow a [[link]] — search current entries, then try other chapters
async function followLink(key) {
  // 1. Try current entries (title match)
  // (a link of a translated text names its target in English: the entry's "canon")
  const found = entries.value.find(
    (e) => (e.title ?? e.name ?? '').toLowerCase() === key || (e.canon ?? '').toLowerCase() === key || e.key === key || e.key?.split('/').pop() === key,
  );
  if (found) {
    if (grouped.value) groupTab.value = Math.max(0, groupNames.value.indexOf(found.tab));
    selected.value = found;
    return;
  }

  if (props.mode === 'rules') {
    // 2. Try other rules chapters
    const chapters = summary.value?.rules ?? [];
    for (const ch of chapters) {
      if (ch.key === type.value) continue;
      try {
        let res;
        if (ch.introOnly) {
          res = await apiGet(`${props.gmPrefix}/content/${ch.key}`, props.token);
          const intro = res.intro;
          const sects = [];
          if (intro?.body) sects.push({ key: '__body__', title: ch.title, body: intro.body });
          (intro?.sections ?? []).forEach((s, i) => sects.push({ key: `__sec__${i}`, title: s.title, canon: s.canon, body: s.body }));
          const hit = sects.find((e) => e.title?.toLowerCase() === key || e.canon?.toLowerCase() === key);
          if (hit) { type.value = ch.key; return; }
        } else {
          res = await apiGet(`${props.gmPrefix}/rules/${ch.key}`, props.token);
          const hit = (res.rules ?? []).find(
            (e) => (e.title ?? '').toLowerCase() === key || (e.canon ?? '').toLowerCase() === key || e.key?.split('/').pop() === key,
          );
          if (hit) { type.value = ch.key; pendingKey = hit.key; return; }
        }
      } catch { /* skip */ }
    }
  }

  // 3. Bubble up to parent (App may switch tabs)
  emit('follow-key', key);
}

onMounted(async () => {
  try {
    summary.value = await apiGet(`${props.gmPrefix}/content`, props.token);
    if (props.mode === 'rules' && !type.value && summary.value?.rules?.length) {
      type.value = summary.value.rules[0].key;
    }
  } catch (e) {
    error.value = e.message;
  }
  load();
});

watch(type, load);
watch(
  () => props.jumpTo,
  (j) => {
    if (!j) return;
    pendingKey = j.key;
    if (type.value !== j.type) {
      type.value = j.type;
    } else {
      load();
    }
  },
);
</script>

<template>
  <section :aria-label="t('Rules')" class="rules-root">
    <!-- toolbar: the sections of the page, and (outside the Reference) the type tabs; the search bar belongs to the Reference -->
    <div class="rules-bar">
      <div v-if="grouped" class="subtabs" role="group" :aria-label="t('Sections')">
        <button v-for="(g, i) in groupNames" :key="g" :aria-pressed="groupTab === i" data-test="group-tab" @click="groupTab = i">{{ g }}</button>
      </div>
      <div v-if="tabbed" class="subtabs" role="group" :aria-label="t('Sections')">
        <button v-for="(e, i) in entries" :key="e.key" :aria-pressed="tab === i" data-test="chapter-tab" @click="tab = i">{{ e.title }}</button>
      </div>
      <div v-if="summary && !hideTabs" class="subtabs" role="group" :aria-label="t('Kind')">
        <template v-if="mode === 'tables'">
          <button v-for="ty in summary.types" :key="ty.id" :aria-pressed="type === ty.id" @click="type = ty.id">
            {{ ty.label }} <span class="small">{{ ty.count }}</span>
          </button>
        </template>
        <template v-else>
          <button
            v-for="r in summary.rules"
            :key="r.key"
            :aria-pressed="type === r.key"
            class="rules-chapter-tab"
            @click="type = r.key"
          >
            {{ r.title }}
          </button>
        </template>
      </div>
    </div>

    <p v-if="error" class="error">{{ error }}</p>

    <!-- inline intro for abilities / professions: full width above list and detail -->
    <div v-if="showInlineIntro" class="inline-intro panel">
      <IntroBlocks :intro="intro" @follow="followLink" />
    </div>

    <!-- tabbed chapter: the tab's own text as its intro, then the rows of its table with the info of the chosen one beside them -->
    <article v-if="textTab" class="rules-detail panel general-info" data-test="tab-text">
      <TextBlocks :text="activeEntry.body" :tables="activeEntry.tables" @follow="followLink" />
      <template v-for="(s, si) in activeEntry.sections" :key="si">
        <h3 class="rules-section-title">{{ s.title }}</h3>
        <TextBlocks :text="s.body" :tables="activeEntry.tables" @follow="followLink" />
      </template>
    </article>
    <template v-else-if="tabbed && activeEntry && !textTab">
      <div v-if="paragraphsOf(activeEntry.body).some((l) => !l.trim().startsWith('{{table:'))" class="inline-intro panel">
        <TextBlocks :text="activeEntry.body" intro @follow="followLink" />
      </div>
      <div class="rows-layout">
        <nav class="rules-list" :aria-label="t('Entries')">
          <p v-if="!shownRows.length" class="muted" style="padding: 8px 12px;">{{ t('Nothing found.') }}</p>
          <button v-for="(r, i) in shownRows" :key="i" class="rules-row" :class="{ active: row === r }" data-test="table-row" @click="row = r">
            <span class="rules-row-name">{{ r.cells[0] }}</span>
          </button>
        </nav>
        <article v-if="row" class="rules-detail panel">
          <h2 class="rules-detail-name">{{ row.cells[0] }}</h2>
          <template v-for="(cell, ci) in row.cells.slice(1)" :key="ci">
            <h3 v-if="ci > 0 && cell" class="rules-section-title">{{ activeTable.columns[ci + 1] }}</h3>
            <TextBlocks :text="cell" @follow="followLink" />
          </template>
        </article>
      </div>
    </template>

    <!-- list + detail layout -->
    <div v-else class="rules-layout" :class="{ 'detail-open': showDetail }">

      <!-- left: the list -->
      <nav class="rules-list" :aria-label="t('Entries')">
        <p v-if="!loading && !shownEntries.length" class="muted" style="padding: 8px 12px;">{{ t('Nothing found.') }}</p>
        <button
          v-for="e in shownEntries"
          :key="e.key"
          class="rules-row"
          :class="{ active: selected?.key === e.key }"
          @click="selected = e"
        >
          <span class="rules-row-name">{{ e.step ? `${e.step}. ${e.name ?? e.title}` : (e.name ?? e.title) }}</span>
          <span v-if="e.subtitle" class="rules-row-sub">{{ e.subtitle }}</span>
        </button>
      </nav>

      <!-- right: the detail -->
      <article v-if="selected" class="rules-detail panel">
        <button class="rules-back link" @click="selected = null">← {{ t('Back') }}</button>

        <div class="rules-detail-head">
          <h2 class="rules-detail-name">{{ selected.step ? `${selected.step}. ${selected.name ?? selected.title}` : (selected.name ?? selected.title) }}</h2>
          <div class="chips" style="margin-top: 4px;">
            <PinButton :snapshot="pinSelected" />
            <span v-if="selected.pack" class="chip" :class="{ homebrew: selected.homebrew }" data-test="source-chip">{{ t('Source') }} · {{ selected.pack }}</span>
            <span v-if="selected.subtitle" class="chip">{{ selected.subtitle }}</span>
          </div>
        </div>

        <p v-if="selected.found === false" class="muted">
          {{ t('This entry is no longer in the loaded content; only its name is known.') }}
        </p>
        <dl v-if="selected.fields?.length" class="fields" style="margin: 10px 0;">
          <template v-for="f in selected.fields" :key="f.label">
            <dt>{{ f.label }}</dt>
            <dd><RuleText :text="f.value" @follow="followLink" /></dd>
          </template>
        </dl>
        <TextBlocks :text="selected.body" :tables="selected.tables" :includes="selected.includes" @follow="followLink" />
        <template v-for="(s, si) in selectedSections" :key="si">
          <h3 class="rules-section-title">{{ s.title }}</h3>
          <TextBlocks :text="s.body" :tables="selected.tables" :includes="selected.includes" @follow="followLink" />
        </template>
        <DataTable v-for="tb in unplacedTables([selected.body, ...selectedSections.map((s) => s.body)], selected.tables)" :key="tb.title" :table="tb" />
        <NpcCreator v-if="selected.tool === 'npc-creator'" :token="token" :gm-prefix="gmPrefix" />
      </article>

      <div v-else-if="!loading && shownEntries.length === 0" class="rules-detail panel muted" style="display:flex;align-items:center;justify-content:center;">
        {{ t('Nothing found.') }}
      </div>
    </div>
  </section>
</template>

<style scoped>
.rules-root { display: flex; flex-direction: column; gap: 10px; }

.rules-bar { display: flex; flex-direction: column; gap: 8px; }

.rules-chapter-tab { font-style: italic; }

/* Intro, always open above the list */
.inline-intro { margin-bottom: 10px; }

/* tabbed chapter: list and info always shown together (stacked on a phone) */
.rows-layout { display: grid; grid-template-columns: 1fr; gap: 10px; }
@media (min-width: 640px) {
  .rows-layout { grid-template-columns: minmax(200px, 300px) 1fr; align-items: start; }
}

/* list + detail side-by-side on wide screens */
.rules-layout { display: grid; grid-template-columns: 1fr; gap: 10px; }

@media (min-width: 640px) {
  .rules-layout { grid-template-columns: minmax(200px, 300px) 1fr; align-items: start; }
  .rules-back { display: none !important; }
}

/* on mobile, switch between list and detail */
@media (max-width: 639px) {
  .rules-layout .rules-list  { display: block; }
  .rules-layout .rules-detail { display: none; }
  .rules-layout.detail-open .rules-list  { display: none; }
  .rules-layout.detail-open .rules-detail { display: block; }
  .rules-back { display: inline-block; margin-bottom: 10px; }
}

.rules-list {
  border: 1px solid var(--line);
  border-radius: var(--radius);
  background: var(--cream);
  overflow: hidden;
  position: sticky;
  top: 0;
  max-height: calc(100vh - 12rem);
  overflow-y: auto;
}

.rules-row {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  gap: 6px;
  width: 100%;
  padding: 9px 12px;
  border: 0;
  border-bottom: 1px solid var(--line);
  background: none;
  font: inherit;
  color: var(--ink);
  cursor: pointer;
  text-align: left;
}
.rules-row:last-child { border-bottom: 0; }
.rules-row:hover { background: var(--parchment); }
.rules-row.active { background: var(--green-dark); color: #f3ead2; }
.rules-row.active .rules-row-sub { color: rgba(243, 234, 210, 0.7); }

.rules-row-name { font-weight: 600; }
.rules-row-sub { font-size: 0.8rem; color: var(--muted); white-space: nowrap; flex-shrink: 0; }

.rules-detail { max-height: calc(100vh - 12rem); overflow-y: auto; }
.rules-detail-head { margin-bottom: 8px; }
.rules-detail-name { font-size: 1.1rem; color: var(--green-dark); text-transform: uppercase;
  letter-spacing: 0.04em; font-family: var(--display); }
.rules-section-title { font-size: 0.95rem; color: var(--brown); margin: 1rem 0 0.4rem; text-transform: uppercase; font-family: var(--display); letter-spacing: 0.03em; }
</style>
