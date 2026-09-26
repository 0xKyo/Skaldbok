<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { apiGet } from '../api.js';

const props = defineProps({ token: { type: String, required: true } });

const summary = ref(null);
const type = ref('spells');
const query = ref('');
const entries = ref([]);
const loading = ref(false);
const error = ref('');
const selected = ref(null); // the entry shown in the detail panel
let timer = null;
let request = 0;

// On mobile, when an entry is selected the list slides away.
const showDetail = computed(() => selected.value !== null);

const paragraphs = computed(() =>
  selected.value ? (selected.value.body ?? '').split('\n').filter((p) => p.trim() !== '') : [],
);

async function load() {
  selected.value = null;
  const mine = ++request;
  loading.value = true;
  try {
    const q = query.value.trim();
    const res = await apiGet(`/content/${type.value}${q ? `?q=${encodeURIComponent(q)}` : ''}`, props.token);
    if (mine !== request) return;
    entries.value = res.entries;
    error.value = '';
    if (entries.value.length) selected.value = entries.value[0];
  } catch (e) {
    if (mine === request) error.value = e.message;
  } finally {
    if (mine === request) loading.value = false;
  }
}

onMounted(async () => {
  try {
    summary.value = await apiGet('/content', props.token);
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
</script>

<template>
  <section aria-label="Rules" class="rules-root">
    <!-- toolbar: search + type tabs -->
    <div class="rules-bar">
      <input v-model="query" type="search" placeholder="Search…" aria-label="Search the rules" class="rules-search" />
      <div v-if="summary" class="subtabs" role="group" aria-label="Kind of rule">
        <button v-for="t in summary.types" :key="t.id" :aria-pressed="type === t.id" @click="type = t.id">
          {{ t.label }} <span class="small">{{ t.count }}</span>
        </button>
      </div>
    </div>

    <p v-if="error" class="error">{{ error }}</p>

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
          <span class="rules-row-name">{{ e.name }}</span>
          <span v-if="e.subtitle" class="rules-row-sub">{{ e.subtitle }}</span>
        </button>
        <p v-if="summary" class="muted small" style="padding: 8px 12px;">
          From: {{ summary.packs.map((p) => p.name).join(', ') }}
        </p>
      </nav>

      <!-- right: the detail -->
      <article v-if="selected" class="rules-detail panel">
        <!-- back button on mobile -->
        <button class="rules-back link" @click="selected = null">← Back</button>

        <div class="rules-detail-head">
          <h2 class="rules-detail-name">{{ selected.name }}</h2>
          <div class="chips" style="margin-top: 4px;">
            <span v-if="selected.source" class="chip" :class="selected.homebrew ? 'homebrew' : 'src'">
              {{ selected.source }}
            </span>
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
        <p v-for="(p, i) in paragraphs" :key="i" style="margin: 0 0 0.6em;">{{ p }}</p>
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
  /* sticky so the list doesn't scroll away on desktop */
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
</style>
