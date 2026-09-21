<script setup>
// "Add a spell…": search the rules a player may look up and pick one. Emits the entry's key and name.
import { onBeforeUnmount, ref, watch } from 'vue';
import { apiGet } from '../api.js';

const props = defineProps({
  token: { type: String, required: true },
  type: { type: String, required: true }, // spells | abilities | weapons | armor | gear
  label: { type: String, required: true },
});
const emit = defineEmits(['pick']);

const open = ref(false);
const query = ref('');
const entries = ref([]);
const error = ref('');
let timer = null;
let request = 0;

async function load() {
  const mine = ++request;
  try {
    const q = query.value.trim();
    const res = await apiGet(`/content/${props.type}${q ? `?q=${encodeURIComponent(q)}` : ''}`, props.token);
    if (mine === request) {
      entries.value = res.entries.slice(0, 40);
      error.value = '';
    }
  } catch (e) {
    if (mine === request) error.value = e.message;
  }
}

watch([open, query], () => {
  clearTimeout(timer);
  if (open.value) timer = setTimeout(load, 200);
});
onBeforeUnmount(() => clearTimeout(timer));

function choose(entry) {
  emit('pick', { key: entry.key, name: entry.name });
  open.value = false;
  query.value = '';
}
</script>

<template>
  <div class="picker">
    <button type="button" class="btn secondary small-btn" :aria-expanded="open" @click="open = !open">{{ open ? 'Close' : label }}</button>
    <div v-if="open" class="picker-panel">
      <input v-model="query" type="search" placeholder="Search…" aria-label="Search the rules" />
      <p v-if="error" class="error small">{{ error }}</p>
      <ul>
        <li v-for="e in entries" :key="e.key">
          <button type="button" @click="choose(e)">
            <span class="name">{{ e.name }}</span> <span class="muted small">{{ e.subtitle }}</span>
          </button>
        </li>
      </ul>
      <p v-if="!entries.length && !error" class="muted small">Nothing found.</p>
    </div>
  </div>
</template>
