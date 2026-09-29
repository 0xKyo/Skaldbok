<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { apiGet } from '../api.js';
import DataTable from './DataTable.vue';
import NpcCreator from './NpcCreator.vue';
import RuleText from './RuleText.vue';
import { tablesMarkedIn, textBlocks, unplacedTables } from '../lib/ruletables.js';

const props = defineProps({
  token: { type: String, required: true },
  jumpTo: { type: Object, default: null }, // {key, type}
  mode: { type: String, default: 'tables' }, // 'tables' | 'rules'
  gmPrefix: { type: String, default: '' },   // '/gm' when used in GM view, '' for player
  hideTabs: { type: Boolean, default: false }, // GM sidebar already picks the category
});
const emit = defineEmits(['follow-key']);

const summary = ref(null);
const type = ref(props.jumpTo?.type ?? (props.mode === 'rules' ? '' : 'spells'));
const query = ref('');
const entries = ref([]);
const intro = ref(null);      // {body, sections} — for inline-intro types
const loading = ref(false);
const error = ref('');
const selected = ref(null);
let pendingKey = props.jumpTo?.key ?? null;
let timer = null;
let request = 0;

const showDetail = computed(() => selected.value !== null);

// The rule chapter object for the currently selected type (null in Tables mode — only active in Rules mode)
const currentChapter = computed(() =>
  props.mode === 'rules'
    ? (summary.value?.rules?.find((r) => r.key === type.value) ?? null)
    : null,
);
const isRulesChapter = computed(() => currentChapter.value !== null);
// An intro-only chapter surfaces a content type's intro as a rules chapter
const isIntroOnly = computed(() => currentChapter.value?.introOnly === true);

// In Tables mode a type's intro is always shown above the list, except spells and skills: theirs lives in the "General info" tab.
const rulesIntroTypes = ['spells', 'skills'];
const showInlineIntro = computed(() =>
  props.mode === 'tables' && !rulesIntroTypes.includes(type.value) && intro.value !== null,
);

const paragraphs = computed(() =>
  selected.value ? (selected.value.body ?? '').split('\n').filter((p) => p.trim() !== '') : [],
);
const selectedSections = computed(() => selected.value?.sections ?? []);

async function load() {
  selected.value = null;
  intro.value = null;
  if (!type.value) return;
  const mine = ++request;
  loading.value = true;
  try {
    const q = query.value.trim();
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
        sects.push({ key: `__sec__${i}`, title: s.title, body: s.body, sections: [], tables: tablesMarkedIn(s.body, all) });
      }
      entries.value = sects;
      if (entries.value.length) {
        selected.value = pendingKey
          ? (entries.value.find((e) => e.key === pendingKey || e.title?.toLowerCase() === pendingKey) ?? entries.value[0])
          : entries.value[0];
        pendingKey = null;
      }
    } else if (isRulesChapter.value) {
      res = await apiGet(`${props.gmPrefix}/rules/${type.value}`, props.token);
      entries.value = res.rules ?? [];
      if (entries.value.length) {
        selected.value = pendingKey
          ? (entries.value.find((e) => e.key === pendingKey || e.title?.toLowerCase() === pendingKey) ?? entries.value[0])
          : entries.value[0];
        pendingKey = null;
      }
    } else {
      res = await apiGet(`${props.gmPrefix}/content/${type.value}${q ? `?q=${encodeURIComponent(q)}` : ''}`, props.token);
      entries.value = res.entries;
      intro.value = res.intro ?? null;
      if (entries.value.length) {
        selected.value = pendingKey
          ? (entries.value.find((e) => e.key === pendingKey) ?? entries.value[0])
          : entries.value[0];
        pendingKey = null;
      }
    }
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
  const found = entries.value.find(
    (e) => (e.title ?? e.name ?? '').toLowerCase() === key || e.key?.split('/').pop() === key,
  );
  if (found) { selected.value = found; return; }

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
          (intro?.sections ?? []).forEach((s, i) => sects.push({ key: `__sec__${i}`, title: s.title, body: s.body }));
          const hit = sects.find((e) => e.title?.toLowerCase() === key);
          if (hit) { type.value = ch.key; return; }
        } else {
          res = await apiGet(`${props.gmPrefix}/rules/${ch.key}`, props.token);
          const hit = (res.rules ?? []).find(
            (e) => (e.title ?? '').toLowerCase() === key || e.key?.split('/').pop() === key,
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
onBeforeUnmount(() => clearTimeout(timer));

watch(type, load);
watch(query, () => {
  clearTimeout(timer);
  timer = setTimeout(load, 250);
});
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
  <section aria-label="Rules" class="rules-root">
    <!-- toolbar: search (tables non-rules types only) + type tabs -->
    <div class="rules-bar">
      <input
        v-if="mode === 'tables' && !isRulesChapter"
        v-model="query"
        type="search"
        placeholder="Search…"
        aria-label="Search"
        class="rules-search"
      />
      <div v-if="summary && !hideTabs" class="subtabs" role="group" aria-label="Kind">
        <template v-if="mode === 'tables'">
          <button v-for="t in summary.types" :key="t.id" :aria-pressed="type === t.id" @click="type = t.id">
            {{ t.label }} <span class="small">{{ t.count }}</span>
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
      <template v-for="(b, i) in textBlocks(intro.body, intro.tables)" :key="'b' + i">
        <DataTable v-if="b.table" :table="b.table" />
        <p v-else class="intro-p"><RuleText :text="b.text" @follow="followLink" /></p>
      </template>
      <template v-for="(s, si) in intro.sections" :key="si">
        <p class="intro-section-name">{{ s.title }}</p>
        <template v-for="(b, pi) in textBlocks(s.body, intro.tables)" :key="pi">
          <DataTable v-if="b.table" :table="b.table" />
          <p v-else class="intro-p"><RuleText :text="b.text" @follow="followLink" /></p>
        </template>
      </template>
      <DataTable v-for="t in unplacedTables([intro.body, ...intro.sections.map((s) => s.body)], intro.tables)" :key="t.title" :table="t" />
    </div>

    <!-- list + detail layout -->
    <div class="rules-layout" :class="{ 'detail-open': showDetail }">

      <!-- left: the list -->
      <nav class="rules-list" aria-label="Entries">
        <p v-if="!loading && !entries.length" class="muted" style="padding: 8px 12px;">Nothing found.</p>
        <button
          v-for="e in entries"
          :key="e.key"
          class="rules-row"
          :class="{ active: selected?.key === e.key }"
          @click="selected = e"
        >
          <span class="rules-row-name">{{ e.step ? `${e.step}. ${e.name ?? e.title}` : (e.name ?? e.title) }}</span>
          <span v-if="e.subtitle" class="rules-row-sub">{{ e.subtitle }}</span>
        </button>
        <p v-if="summary && mode === 'tables'" class="muted small" style="padding: 8px 12px;">
          From: {{ summary.packs.map((p) => p.name).join(', ') }}
        </p>
      </nav>

      <!-- right: the detail -->
      <article v-if="selected" class="rules-detail panel">
        <button class="rules-back link" @click="selected = null">← Back</button>

        <div class="rules-detail-head">
          <h2 class="rules-detail-name">{{ selected.step ? `${selected.step}. ${selected.name ?? selected.title}` : (selected.name ?? selected.title) }}</h2>
          <div class="chips" style="margin-top: 4px;">
            <span v-if="selected.homebrew" class="chip homebrew">{{ selected.source }}</span>
            <span v-if="selected.subtitle" class="chip">{{ selected.subtitle }}</span>
          </div>
        </div>

        <p v-if="selected.found === false" class="muted">
          This entry is no longer in the loaded content; only its name is known.
        </p>
        <dl v-if="selected.fields?.length" class="fields" style="margin: 10px 0;">
          <template v-for="f in selected.fields" :key="f.label">
            <dt>{{ f.label }}</dt>
            <dd>{{ f.value }}</dd>
          </template>
        </dl>
        <template v-for="(b, i) in textBlocks(selected.body, selected.tables)" :key="i">
          <DataTable v-if="b.table" :table="b.table" />
          <p v-else style="margin: 0 0 0.6em;"><RuleText :text="b.text" @follow="followLink" /></p>
        </template>
        <template v-for="(s, si) in selectedSections" :key="si">
          <h3 class="rules-section-title">{{ s.title }}</h3>
          <template v-for="(b, pi) in textBlocks(s.body, selected.tables)" :key="pi">
            <DataTable v-if="b.table" :table="b.table" />
            <p v-else style="margin: 0 0 0.6em;"><RuleText :text="b.text" @follow="followLink" /></p>
          </template>
        </template>
        <DataTable v-for="t in unplacedTables([selected.body, ...selectedSections.map((s) => s.body)], selected.tables)" :key="t.title" :table="t" />
        <NpcCreator v-if="selected.tool === 'npc-creator'" :token="token" :gm-prefix="gmPrefix" />
      </article>

      <div v-else-if="!loading && entries.length === 0" class="rules-detail panel muted" style="display:flex;align-items:center;justify-content:center;">
        Nothing found.
      </div>
    </div>
  </section>
</template>

<style scoped>
.rules-root { display: flex; flex-direction: column; gap: 10px; }

.rules-bar { display: flex; flex-direction: column; gap: 8px; }
.rules-search { width: 100%; background: #fbf6e6; border: 1px solid var(--line); color: var(--ink);
  border-radius: 8px; padding: 10px 12px; font: inherit; }
.rules-search:focus-visible { outline: 2px solid var(--green); }

.rules-chapter-tab { font-style: italic; }

/* Intro, always open above the list */
.inline-intro { margin-bottom: 10px; }
.intro-p { font-size: 0.88rem; color: var(--ink); margin: 0 0 0.5em; line-height: 1.5; }
.intro-section-name { font-size: 0.8rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.04em; color: var(--brown); margin: 0.6em 0 0.2em; }

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
