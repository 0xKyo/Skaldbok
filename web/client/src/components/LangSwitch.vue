<script setup>
// The language of the page, for this browser. Everything the page shows comes again in the new one, so it simply reloads.
import { LANGUAGES, lang, setLang, t } from '../i18n.js';

function choose(id) {
  if (id === lang.value) return;
  setLang(id);
  window.location.reload();
}
</script>

<template>
  <select class="lang" :value="lang" :aria-label="t('Language')" :title="t('Language')" data-test="lang" @change="choose($event.target.value)">
    <option v-for="l in LANGUAGES" :key="l.id" :value="l.id">{{ l.id.toUpperCase() }} · {{ l.name }}</option>
  </select>
</template>

<style scoped>
.lang { font: inherit; font-size: 0.8rem; padding: 2px 4px; border: 1px solid var(--line); border-radius: 6px; background: transparent; color: inherit; max-width: 9.5rem; }
</style>
