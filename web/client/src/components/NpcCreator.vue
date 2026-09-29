<script setup>
// Random NPC: one pick from each list of the rulebook's "Creating NPCs" (data/system/npcs.yaml); each result can be re-rolled alone.
import { onMounted, ref } from 'vue';
import { apiGet } from '../api.js';

const props = defineProps({
  token: { type: String, required: true },
  gmPrefix: { type: String, default: '/gm' },
});

const lists = ref([]);   // [{key, dice, values[]}]
const npc = ref(null);   // {key: value}
const error = ref('');

const label = (key) => key.charAt(0).toUpperCase() + key.slice(1);

function pick(list, except = null) {
  const pool = list.values.length > 1 ? list.values.filter((v) => v !== except) : list.values;
  return pool[Math.floor(Math.random() * pool.length)];
}

function create() {
  npc.value = Object.fromEntries(lists.value.map((l) => [l.key, pick(l)]));
}

function reroll(list) {
  npc.value = { ...npc.value, [list.key]: pick(list, npc.value[list.key]) };
}

onMounted(async () => {
  try {
    lists.value = (await apiGet(`${props.gmPrefix}/npcs`, props.token)).lists ?? [];
    if (!lists.value.length) error.value = 'The NPC lists (data/system/npcs.yaml) are not loaded.';
  } catch (e) {
    error.value = e.message ?? 'Could not load the NPC lists.';
  }
});
</script>

<template>
  <div class="npc-creator">
    <p v-if="error" class="error">{{ error }}</p>
    <button v-else class="create-btn" data-test="npc-create" @click="create">Create Random NPC</button>

    <div v-if="npc" class="npc-card panel" data-test="npc">
      <div v-for="l in lists" :key="l.key" class="npc-row">
        <span class="npc-key">{{ label(l.key) }}</span>
        <span class="npc-val" data-test="npc-value">{{ npc[l.key] }}</span>
        <button class="reroll" :aria-label="`Re-roll ${l.key}`" @click="reroll(l)">Re-roll</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.npc-creator { margin-top: 1rem; }
.create-btn { padding: 8px 18px; border: 1px solid var(--green-dark); border-radius: 20px; background: var(--green-dark);
  color: #f3ead2; font: inherit; font-weight: 600; cursor: pointer; }
.create-btn:hover { background: var(--green); }
.npc-card { margin-top: 12px; padding: 4px 14px; }
.npc-row { display: grid; grid-template-columns: 7.5rem 1fr auto; align-items: baseline; gap: 10px; padding: 8px 0; border-bottom: 1px solid var(--line); }
.npc-row:last-child { border-bottom: none; }
.npc-key { font-family: var(--display); font-weight: 800; font-size: 0.72rem; letter-spacing: 0.12em; text-transform: uppercase; color: var(--brown); }
.npc-val { font-size: 1rem; }
.reroll { padding: 2px 12px; border: 1px solid var(--line); border-radius: 20px; background: none; color: var(--muted); font: inherit; font-size: 0.8rem; cursor: pointer; }
.reroll:hover { color: var(--green-dark); border-color: var(--green-dark); }
.error { color: var(--danger, #c0392b); }
</style>
