<script setup>
import { onMounted, ref } from 'vue';
import { apiGet } from '../api.js';

const props = defineProps({
  token: { type: String, required: true },
});

const gmLink = ref('');
const characters = ref([]);
const loading = ref(true);
const error = ref('');
const copied = ref('');

onMounted(async () => {
  try {
    const data = await apiGet('/gm/links', props.token);
    gmLink.value = data.gmLink ?? '';
    characters.value = data.characters ?? [];
  } catch (e) {
    error.value = e.message ?? 'Could not load links.';
  } finally {
    loading.value = false;
  }
});

async function copy(text, key) {
  try {
    await navigator.clipboard.writeText(text);
    copied.value = key;
    setTimeout(() => { copied.value = ''; }, 1800);
  } catch {
    /* clipboard may be blocked */
  }
}
</script>

<template>
  <div class="settings-wrap">
    <h2 class="settings-title">General Settings</h2>

    <div v-if="loading" class="muted">Loading…</div>
    <div v-else-if="error" class="error">{{ error }}</div>

    <template v-else>
      <!-- GM link -->
      <section class="settings-section">
        <h3 class="section-label">GM Link</h3>
        <p class="section-hint">Open this link in your browser to access the GM interface.</p>
        <div class="link-row">
          <input class="link-input" :value="gmLink" readonly />
          <button class="copy-btn" @click="copy(gmLink, 'gm')">
            {{ copied === 'gm' ? 'Copied!' : 'Copy' }}
          </button>
        </div>
      </section>

      <!-- Player links -->
      <section class="settings-section">
        <h3 class="section-label">Player Links</h3>
        <p class="section-hint">Send each player their personal link. It only opens their own character.</p>
        <div v-if="characters.length === 0" class="muted small">No characters yet. Create one in the Characters list.</div>
        <div v-for="c in characters" :key="c.id" class="link-row">
          <span class="link-name">{{ c.name || c.id }}</span>
          <input class="link-input" :value="c.link" readonly />
          <button class="copy-btn" @click="copy(c.link, c.id)">
            {{ copied === c.id ? 'Copied!' : 'Copy' }}
          </button>
        </div>
      </section>
    </template>
  </div>
</template>

<style scoped>
.settings-wrap {
  padding: 1.5rem 2rem;
  max-width: 680px;
}

.settings-title {
  font-size: 1.1rem;
  font-weight: 600;
  margin: 0 0 1.5rem;
  color: var(--text);
}

.settings-section {
  margin-bottom: 2rem;
}

.section-label {
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--muted);
  margin: 0 0 0.25rem;
}

.section-hint {
  font-size: 0.85rem;
  color: var(--muted);
  margin: 0 0 0.75rem;
}

.link-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.link-name {
  min-width: 120px;
  font-size: 0.9rem;
  font-weight: 500;
  flex-shrink: 0;
}

.link-input {
  flex: 1;
  min-width: 0;
  font-size: 0.8rem;
  font-family: monospace;
  padding: 4px 8px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--input-bg, #f8f8f8);
  color: var(--muted);
}

.copy-btn {
  flex-shrink: 0;
  font-size: 0.8rem;
  padding: 4px 12px;
  border: 1px solid var(--border);
  border-radius: 4px;
  cursor: pointer;
  background: var(--btn-bg, #f0f0f0);
  color: var(--text);
  transition: background 0.15s;
  min-width: 64px;
}
.copy-btn:hover { background: var(--hover-bg, #e0e0e0); }

.muted { color: var(--muted); }
.small { font-size: 0.85rem; }
.error { color: var(--danger, #c0392b); font-size: 0.9rem; }
</style>
