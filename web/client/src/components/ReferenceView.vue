<script setup>
// The search bar, then index + panel side by side (stacked on narrow screens). Same pieces the GM sidebar uses.
// The search is one for the whole Reference: with "Only in <category>" checked it filters the open category, otherwise it lists matches from everywhere.
import { computed, inject, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { createGlobalSearch } from '../lib/globalSearch.js';
import ReferenceIndex from './ReferenceIndex.vue';
import ReferencePanel from './ReferencePanel.vue';

const props = defineProps({
  state: { type: Object, required: true }, // from useReference()
  token: { type: String, required: true },
  gmPrefix: { type: String, default: '' },
});

// the page open (and the entry of its list) is one of the things the ◀ ▶ buttons remember
const nav = inject('nav', null);
const here = () => (props.state.current ? JSON.parse(JSON.stringify(props.state.current)) : null);
function putBack() {
  const s = nav.slice('ref');
  props.state.current = s ? { ...s, entryKey: s.seen ?? s.entryKey ?? null } : null;
}
if (nav) {
  watch(() => props.state.current && JSON.stringify(props.state.current), () => nav.record('ref', here()), { immediate: true });
  watch(() => nav.tick.value, putBack);
  onMounted(() => { if (nav.restoring.value) putBack(); });
}

// ---- the search ----
const text = computed(() => (props.state.query ?? '').trim());
const only = computed(() => props.state.only !== false);
const everywhere = computed(() => !!text.value && !(only.value && props.state.current));     // the results take the place of the page
const results = ref([]);
const searching = ref(false);
const engine = createGlobalSearch(() => props.token, props.gmPrefix);
let timer = null;
let asked = 0;

watch([text, everywhere], () => {
  clearTimeout(timer);
  if (!everywhere.value) {
    results.value = [];
    searching.value = false;
    return;
  }
  searching.value = true;
  timer = setTimeout(async () => {
    const mine = ++asked;
    const found = await engine.run(text.value, props.state);
    if (mine !== asked) return;
    results.value = found;
    searching.value = false;
  }, 300);
});
onBeforeUnmount(() => clearTimeout(timer));

// a result opens its page and keeps the search on that page, so the rest of what matched is still in the list
function open(r) {
  props.state.only = true;
  props.state.goTo(r.mode === 'rules' ? { key: r.type, entry: r.key } : { key: r.key, type: r.type });
}
</script>

<template>
  <section class="ref-view" :class="{ 'is-open': !!state.current, searching: everywhere }" aria-label="Rules">
    <div class="ref-search">
      <input v-model="state.query" type="search" placeholder="Search…" aria-label="Search" class="ref-search-box" data-test="ref-search" />
      <label v-if="state.current" class="ref-only" data-test="ref-only-label">
        <input v-model="state.only" type="checkbox" data-test="ref-only" /> Only in {{ state.current.label }}
      </label>
    </div>
    <ReferenceIndex class="ref-view-index" :state="state" />
    <button v-if="state.current" type="button" class="ref-back link" data-test="reference-back" @click="state.current = null">← All categories</button>
    <div v-if="everywhere" class="ref-results panel" data-test="ref-results">
      <p v-if="searching" class="muted small">Searching…</p>
      <p v-else-if="!results.length" class="muted small" data-test="ref-none">Nothing found for “{{ text }}”.</p>
      <ul v-else class="ref-hits">
        <li v-for="(r, i) in results" :key="i">
          <button type="button" class="ref-hit" data-test="ref-hit" @click="open(r)">
            <span class="ref-hit-title">{{ r.title }}</span>
            <span v-if="r.note" class="ref-hit-note">{{ r.note }}</span>
            <span class="ref-hit-cat">{{ r.category }}</span>
          </button>
        </li>
      </ul>
    </div>
    <ReferencePanel v-else class="ref-view-panel" :state="state" :token="token" :gm-prefix="gmPrefix" />
  </section>
</template>

<style scoped>
.ref-view { display: grid; grid-template-columns: 1fr; gap: 12px; align-items: start; }
.ref-search { grid-column: 1 / -1; display: flex; align-items: center; gap: 14px; flex-wrap: wrap; }
.ref-search-box { flex: 1 1 260px; min-width: 0; background: #fbf6e6; border: 1px solid var(--line); color: var(--ink); border-radius: 8px; padding: 10px 12px; font: inherit; }
.ref-search-box:focus-visible { outline: 2px solid var(--green); }
.ref-only { display: inline-flex; align-items: center; gap: 6px; font-size: 0.85rem; color: var(--muted); cursor: pointer; white-space: nowrap; }
@media (min-width: 720px) {
  .ref-view { grid-template-columns: 190px minmax(0, 1fr); }
}
.ref-view-panel, .ref-results { min-width: 0; }
.ref-hits { list-style: none; margin: 0; padding: 0; }
.ref-hit { display: flex; align-items: baseline; gap: 10px; width: 100%; text-align: left; background: none; border: 0; border-bottom: 1px solid var(--line); padding: 8px 4px; font: inherit; color: var(--ink); cursor: pointer; }
.ref-hit:hover { background: var(--parchment); }
.ref-hit-title { font-weight: 600; }
.ref-hit-note { color: var(--muted); font-size: 0.8rem; flex: 1; }
.ref-hit-cat { margin-left: auto; font-size: 0.72rem; font-weight: 700; letter-spacing: 0.04em; text-transform: uppercase; color: var(--green-dark); border: 1px solid var(--green-dark); border-radius: 10px; padding: 1px 8px; white-space: nowrap; }
/* on a phone the index and a category take turns (the category used to open below the whole index, out of sight) */
.ref-back { justify-self: start; }
@media (min-width: 720px) { .ref-back { display: none; } }
@media (max-width: 719px) { .ref-view.is-open .ref-view-index, .ref-view.searching .ref-view-index { display: none; } }
</style>
