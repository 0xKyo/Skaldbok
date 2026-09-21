<script setup>
// The rules a player may look up: spells, abilities, skills, kin, professions and equipment (creatures are the GM's).
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { apiGet } from '../api.js';
import EntryCard from './EntryCard.vue';

const props = defineProps({ token: { type: String, required: true } });

const summary = ref(null);
const type = ref('spells');
const query = ref('');
const entries = ref([]);
const loading = ref(false);
const error = ref('');
let timer = null;
let request = 0;

async function load() {
  const mine = ++request; // a slow answer must not overwrite a newer one
  loading.value = true;
  try {
    const q = query.value.trim();
    const res = await apiGet(`/content/${type.value}${q ? `?q=${encodeURIComponent(q)}` : ''}`, props.token);
    if (mine !== request) return;
    entries.value = res.entries;
    error.value = '';
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
  <section aria-label="Rules">
    <input v-model="query" type="search" placeholder="Search the rules…" aria-label="Search the rules" />
    <div v-if="summary" class="subtabs" role="group" aria-label="Kind of rule">
      <button v-for="t in summary.types" :key="t.id" :aria-pressed="type === t.id" @click="type = t.id">{{ t.label }} <span class="small">{{ t.count }}</span></button>
    </div>
    <p v-if="error" class="error">{{ error }}</p>
    <p v-else-if="!loading && !entries.length" class="muted">Nothing found.</p>
    <EntryCard v-for="e in entries" :key="e.key" :entry="e" />
    <p v-if="summary" class="muted small">
      From: {{ summary.packs.map((p) => p.name).join(', ') }}. Homebrew content is marked.
    </p>
  </section>
</template>
