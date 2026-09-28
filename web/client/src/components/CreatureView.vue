<script setup>
// GM-only creature catalog: searchable list on the left, full stat block on the right.
import { onMounted, ref, watch } from 'vue';
import { apiGet } from '../api.js';

const props = defineProps({
  token: { type: String, required: true },
});

const list = ref([]);       // [{id, name, sub}]
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
  <div class="creature-layout">
    <!-- list panel -->
    <div class="creature-list-panel">
      <div class="search-row">
        <input
          v-model="query"
          class="search-input"
          placeholder="Search creatures…"
          type="search"
        />
      </div>
      <div v-if="loading" class="muted small" style="padding: 8px 12px;">Loading…</div>
      <ul class="creature-list">
        <li
          v-for="c in list"
          :key="c.id"
          class="creature-item"
          :class="{ active: selected?.id === c.id }"
          @click="pick(c)"
        >
          <span class="c-name">{{ c.name }}</span>
          <span v-if="c.sub" class="c-sub">{{ c.sub }}</span>
        </li>
      </ul>
    </div>

    <!-- detail panel -->
    <div class="creature-detail-panel">
      <div v-if="loadingDetail" class="muted" style="padding: 1.5rem;">Loading…</div>
      <div v-else-if="!selected" class="muted placeholder">Select a creature.</div>
      <div v-else class="creature-detail">
        <!-- header -->
        <div class="detail-header">
          <h2 class="detail-name">{{ selected.name }}</h2>
          <span class="detail-meta">
            {{ selected.kind === 'npc' ? 'NPC' : selected.kind === 'animal' ? 'Animal' : 'Creature' }}
            <template v-if="selected.category"> · {{ selected.category }}</template>
          </span>
        </div>

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
</template>

<style scoped>
.creature-layout {
  display: flex;
  flex: 1;
  overflow: hidden;
  height: 100%;
}

/* ---- list panel ---- */
.creature-list-panel {
  width: 220px;
  flex-shrink: 0;
  border-right: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.search-row {
  padding: 6px 8px;
  border-bottom: 1px solid var(--border);
}

.search-input {
  width: 100%;
  box-sizing: border-box;
  font-size: 0.85rem;
  padding: 4px 8px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--input-bg, #fff);
  color: var(--text);
}

.creature-list {
  list-style: none;
  margin: 0;
  padding: 0;
  overflow-y: auto;
  flex: 1;
}

.creature-item {
  padding: 5px 12px;
  cursor: pointer;
  border-left: 3px solid transparent;
}
.creature-item:hover { background: var(--hover-bg, #f5f5f5); }
.creature-item.active {
  background: var(--active-bg, #e8f0fe);
  border-left-color: var(--accent, #2d7a68);
}

.c-name { display: block; font-size: 0.88rem; font-weight: 500; }
.c-sub { display: block; font-size: 0.73rem; color: var(--muted); }

/* ---- detail panel ---- */
.creature-detail-panel {
  flex: 1;
  overflow-y: auto;
}

.placeholder {
  padding: 2rem;
  font-style: italic;
}

.creature-detail { padding: 1.25rem 1.5rem; max-width: 700px; }

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
