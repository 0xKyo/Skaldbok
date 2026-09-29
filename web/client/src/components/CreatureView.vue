<script setup>
// GM-only creature catalog: searchable list on the left, full stat block on the right.
import { computed, onMounted, ref, watch } from 'vue';
import { apiGet } from '../api.js';
import DataTable from './DataTable.vue';
import MessageImage from './MessageImage.vue';
import RuleText from './RuleText.vue';
import { textBlocks, unplacedTables } from '../lib/ruletables.js';

const props = defineProps({
  token: { type: String, required: true },
});

const TAGS = [{ id: 'npc', label: 'NPC' }, { id: 'animal', label: 'Animal' }, { id: 'monster', label: 'Monster' }];

const list = ref([]);       // [{id, key, name, sub, kind}]
const intro = ref(null);    // the Bestiary's general text: the General Info tab
const tab = ref('list');    // 'list' | 'info'
const showList = computed(() => tab.value === 'list' || !intro.value);
const tags = ref([]);       // active tag filters; none = every creature
const shown = computed(() => (tags.value.length ? list.value.filter((c) => tags.value.includes(c.kind)) : list.value));

function toggleTag(id) {
  tags.value = tags.value.includes(id) ? tags.value.filter((t) => t !== id) : [...tags.value, id];
}
const selected = ref(null); // full monster detail
const query = ref('');
const loading = ref(false);
const loadingDetail = ref(false);
let searchTimer = null;

async function search() {
  loading.value = true;
  try {
    const q = query.value.trim();
    const data = await apiGet(`/gm/creatures${q ? `?q=${encodeURIComponent(q)}` : ''}`, props.token);
    list.value = data.creatures ?? [];
    intro.value = data.intro ?? null;
    if (selected.value && !list.value.find((c) => c.id === selected.value.id)) selected.value = null;
  } finally {
    loading.value = false;
  }
}

async function pick(item) {
  if (selected.value?.id === item.id) return;
  loadingDetail.value = true;
  selected.value = null;
  try {
    selected.value = await apiGet(`/gm/creatures/${encodeURIComponent(item.key)}`, props.token);
  } finally {
    loadingDetail.value = false;
  }
}

watch(query, () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(search, 280);
});

onMounted(search);
</script>

<template>
  <section class="creature-page" aria-label="Creatures">
    <!-- same page as every category: tabs, search, (filter tags), then the list with the description beside it; the Bestiary text is the General Info tab -->
    <div v-if="intro" class="subtabs" role="group" aria-label="Sections">
      <button :aria-pressed="tab === 'list'" data-test="creature-tab" @click="tab = 'list'">Creatures</button>
      <button :aria-pressed="tab === 'info'" data-test="creature-tab" @click="tab = 'info'">General Info</button>
    </div>

    <input v-show="showList" v-model="query" class="c-search" placeholder="Search…" aria-label="Search" type="search" />
    <div v-show="showList" class="tag-row" role="group" aria-label="Creature type">
      <button v-for="t in TAGS" :key="t.id" class="tag" :aria-pressed="tags.includes(t.id)" data-test="creature-tag" @click="toggleTag(t.id)">{{ t.label }}</button>
    </div>

    <div v-if="intro && !showList" class="panel c-intro">
      <template v-for="(b, i) in textBlocks(intro.body, intro.tables)" :key="'b' + i">
        <DataTable v-if="b.table" :table="b.table" />
        <ul v-else-if="b.list" class="rule-list"><li v-for="(it, k) in b.list" :key="k"><RuleText :text="it" /></li></ul>
        <p v-else class="intro-p"><RuleText :text="b.text" /></p>
      </template>
      <template v-for="(s, si) in intro.sections" :key="si">
        <p class="intro-section-name">{{ s.title }}</p>
        <template v-for="(b, pi) in textBlocks(s.body, intro.tables)" :key="pi">
          <DataTable v-if="b.table" :table="b.table" />
          <ul v-else-if="b.list" class="rule-list"><li v-for="(it, k) in b.list" :key="k"><RuleText :text="it" /></li></ul>
          <p v-else class="intro-p"><RuleText :text="b.text" /></p>
        </template>
      </template>
      <DataTable v-for="t in unplacedTables([intro.body, ...intro.sections.map((s) => s.body)], intro.tables)" :key="t.title" :table="t" />
    </div>

    <div v-show="showList" class="c-layout">
      <nav class="c-list" aria-label="Creatures">
        <p v-if="loading" class="muted small" style="padding: 8px 12px;">Loading…</p>
        <p v-else-if="!shown.length" class="muted" style="padding: 8px 12px;">Nothing found.</p>
        <button
          v-for="c in shown"
          :key="c.id"
          class="c-row"
          :class="{ active: selected?.id === c.id }"
          @click="pick(c)"
        >
          <span class="c-name">{{ c.name }}</span>
          <span v-if="c.sub" class="c-sub">{{ c.sub }}</span>
        </button>
      </nav>

      <div class="creature-detail-panel panel">
      <div v-if="loadingDetail" class="muted" style="padding: 1.5rem;">Loading…</div>
      <div v-else-if="!selected" class="muted placeholder">Select a creature.</div>
      <div v-else class="creature-detail">
        <!-- header -->
        <div class="detail-header">
          <h2 class="detail-name">{{ selected.name }}</h2>
          <span class="detail-meta">
            {{ selected.kind === 'npc' ? 'NPC' : selected.kind === 'animal' ? 'Animal' : 'Monster' }}
            <template v-if="selected.category"> · {{ selected.category }}</template>
          </span>
          <span v-if="selected.pack" class="chip" style="margin-left: 8px;" data-test="source-chip">Source · {{ selected.pack }}</span>
        </div>

        <div class="stat-row" :class="{ 'has-image': selected.image }">
          <div class="stat-col">
            <!-- stats ref note -->
            <p v-if="selected.statsRef" class="stats-ref">
              <em>Stats: {{ selected.statsRef }}</em>
            </p>

            <!-- stat blocks -->
            <div v-for="(block, bi) in selected.blocks" :key="bi" class="stat-block">
              <h4 class="block-title">{{ block.variant || 'Stat block' }}</h4>
              <table class="fields-table">
                <tbody>
                  <tr v-for="f in block.fields" :key="f.label">
                    <th>{{ f.label }}</th>
                    <td>{{ f.value }}</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
          <MessageImage v-if="selected.image" :key="selected.key" class="creature-image" :token="token" :path="selected.image" :alt="selected.name" />
        </div>

        <!-- attacks -->
        <div v-if="selected.attacks.length" class="section">
          <h4 class="section-title">Attacks</h4>
          <table class="attacks-table">
            <thead>
              <tr><th>Roll</th><th>Attack</th><th>Effect</th></tr>
            </thead>
            <tbody>
              <tr v-for="(a, ai) in selected.attacks" :key="ai">
                <td class="roll-cell">{{ a.rollText || `${a.rollMin}–${a.rollMax}` }}</td>
                <td>{{ a.name }}</td>
                <td>{{ a.text }}</td>
              </tr>
            </tbody>
          </table>
        </div>

        <!-- abilities -->
        <div v-if="selected.abilities.length" class="section">
          <h4 class="section-title">Abilities</h4>
          <div v-for="a in selected.abilities" :key="a.name" class="ability-row">
            <span class="ability-name">{{ a.kind === 'pc_ability' ? 'PC: ' : '' }}{{ a.name }}</span>
            <span class="ability-text">{{ a.text }}</span>
          </div>
        </div>

        <!-- flavour text -->
        <div v-if="selected.quote || selected.description" class="section flavour">
          <p v-if="selected.quote" class="quote">{{ selected.quote }}</p>
          <p v-if="selected.description" class="description">{{ selected.description }}</p>
        </div>

        <!-- extra sections -->
        <details v-if="selected.randomEncounter" class="extra-section">
          <summary>Random encounter</summary>
          <p>{{ selected.randomEncounter }}</p>
        </details>
        <details v-if="selected.adventureSeed" class="extra-section">
          <summary>Adventure seed</summary>
          <p>{{ selected.adventureSeed }}</p>
        </details>
      </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.creature-page { display: flex; flex-direction: column; gap: 10px; }

.c-search { width: 100%; background: #fbf6e6; border: 1px solid var(--line); color: var(--ink);
  border-radius: 8px; padding: 10px 12px; font: inherit; }
.c-search:focus-visible { outline: 2px solid var(--green); }

.c-intro .intro-p { font-size: 0.88rem; color: var(--ink); margin: 0 0 0.5em; line-height: 1.5; }
.c-intro .intro-section-name { font-size: 0.8rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.04em; color: var(--brown); margin: 0.6em 0 0.2em; }

/* list + detail side by side, like every category */
.c-layout { display: grid; grid-template-columns: 1fr; gap: 10px; }
@media (min-width: 640px) {
  .c-layout { grid-template-columns: minmax(200px, 300px) 1fr; align-items: start; }
}

.c-list {
  border: 1px solid var(--line);
  border-radius: var(--radius);
  background: var(--cream);
  overflow: hidden;
  position: sticky;
  top: 0;
  max-height: calc(100vh - 12rem);
  overflow-y: auto;
}
.c-row {
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
.c-row:last-child { border-bottom: 0; }
.c-row:hover { background: var(--parchment); }
.c-row.active { background: var(--green-dark); color: #f3ead2; }
.c-row.active .c-sub { color: rgba(243, 234, 210, 0.7); }

.c-name { font-weight: 600; }
.c-sub { font-size: 0.8rem; color: var(--muted); white-space: nowrap; flex-shrink: 0; }

/* ---- detail panel ---- */
.creature-detail-panel { max-height: calc(100vh - 12rem); overflow-y: auto; }

.placeholder {
  padding: 2rem;
  font-style: italic;
}

.creature-detail { max-width: 700px; }

.tag-row { display: flex; flex-wrap: wrap; gap: 5px; padding: 6px 8px; border-bottom: 1px solid var(--border); }
.tag { background: var(--parchment); color: var(--muted); border: 1px solid var(--line); border-radius: 999px; padding: 2px 10px;
  font: inherit; font-size: 0.78rem; cursor: pointer; }
.tag[aria-pressed='true'] { color: #fff; background: var(--green); border-color: var(--green-dark); }
.stat-row { display: grid; grid-template-columns: 1fr; gap: 0 1.25rem; align-items: start; }
.stat-col { min-width: 0; }
.creature-image { max-height: 320px; max-width: 100%; object-fit: contain; margin: 0 0 1rem; }
@media (min-width: 560px) {
  .stat-row.has-image { grid-template-columns: minmax(0, 1fr) 220px; }
  .stat-row.has-image .creature-image { max-width: 220px; width: 100%; }
}
.detail-header { margin-bottom: 0.75rem; }
.detail-name { font-size: 1.25rem; margin: 0 0 2px; }
.detail-meta { font-size: 0.85rem; color: var(--muted); }

.stats-ref { font-size: 0.85rem; color: var(--muted); margin: 0 0 1rem; }

/* stat blocks */
.stat-block { margin-bottom: 1rem; }
.block-title {
  font-size: 0.8rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--accent, #2d7a68);
  margin: 0 0 4px;
}
.fields-table { border-collapse: collapse; font-size: 0.88rem; }
.fields-table th {
  text-align: left;
  padding: 2px 16px 2px 0;
  color: var(--muted);
  font-weight: 600;
  white-space: nowrap;
}
.fields-table td { padding: 2px 0; }

/* attacks */
.section { margin-top: 1rem; }
.section-title {
  font-size: 0.8rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--accent, #2d7a68);
  margin: 0 0 6px;
}
.attacks-table { border-collapse: collapse; width: 100%; font-size: 0.86rem; }
.attacks-table th {
  text-align: left;
  padding: 2px 12px 4px 0;
  color: var(--muted);
  font-size: 0.78rem;
  font-weight: 600;
  border-bottom: 1px solid var(--border);
}
.attacks-table td { padding: 3px 12px 3px 0; vertical-align: top; }
.roll-cell { white-space: nowrap; color: var(--muted); }

/* abilities */
.ability-row { margin-bottom: 4px; font-size: 0.88rem; }
.ability-name { font-weight: 600; margin-right: 6px; }
.ability-text { color: var(--muted); }

/* flavour */
.flavour { border-top: 1px solid var(--border); padding-top: 0.75rem; }
.quote { font-style: italic; color: var(--muted); margin: 0 0 0.5rem; }
.description { margin: 0; font-size: 0.9rem; }

/* extra sections */
.extra-section { margin-top: 0.75rem; font-size: 0.88rem; }
.extra-section summary { cursor: pointer; font-weight: 600; color: var(--muted); }
.extra-section p { margin: 4px 0 0 1rem; }

.muted { color: var(--muted); }
.small { font-size: 0.85rem; }

@media (max-width: 600px) {
  .creature-layout { flex-direction: column; }
  .creature-list-panel { width: 100%; max-height: 200px; border-right: none; border-bottom: 1px solid var(--border); }
}
</style>
