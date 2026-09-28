<script setup>
// Renders rule body text with [[link|key]] and {{key: term}} markup replaced by clickable spans.
// Emits 'follow' with the canonical key when a link is clicked.

const props = defineProps({
  text: { type: String, required: true },
});
const emit = defineEmits(['follow']);

function parse(str) {
  const parts = [];
  let rest = str;
  while (rest.length > 0) {
    // [[display|key]] or [[display]] (link to a rule/entry)
    let m = rest.match(/^\[\[([^\]|]+?)(?:\|([^\]]+?))?\]\]/);
    if (m) {
      const display = m[1].trim();
      const key = (m[2] || m[1]).trim().toLowerCase();
      parts.push({ type: 'link', text: display, key });
      rest = rest.slice(m[0].length);
      continue;
    }
    // {{key: display | key}} or {{key: display}} (keyword/glossary reference)
    m = rest.match(/^\{\{key:\s*([^|}]+?)(?:\s*\|\s*([^}]+?))?\s*\}\}/);
    if (m) {
      const display = m[1].trim();
      const key = (m[2] || m[1]).trim().toLowerCase();
      parts.push({ type: 'link', text: display, key });
      rest = rest.slice(m[0].length);
      continue;
    }
    // {{table: ...}} — suppress inline; the table renders separately
    m = rest.match(/^\{\{table:[^}]+\}\}/);
    if (m) { rest = rest.slice(m[0].length); continue; }
    // Plain text up to next [[ or {{
    m = rest.match(/^((?:[^[{]|\[(?!\[)|\{(?!\{))+)/);
    if (m) { parts.push({ type: 'text', text: m[1] }); rest = rest.slice(m[0].length); continue; }
    // Fallback: consume one char to avoid infinite loop
    parts.push({ type: 'text', text: rest[0] });
    rest = rest.slice(1);
  }
  return parts;
}
</script>

<template>
  <span class="rule-text">
    <template v-for="(part, i) in parse(text)" :key="i">
      <button v-if="part.type === 'link'" class="rule-link" @click="emit('follow', part.key)">{{ part.text }}</button>
      <template v-else>{{ part.text }}</template>
    </template>
  </span>
</template>

<style scoped>
.rule-text { white-space: pre-wrap; }
.rule-link {
  display: inline;
  background: none;
  border: none;
  padding: 0;
  font: inherit;
  color: var(--teal, #2e7d77);
  text-decoration: underline;
  text-decoration-color: color-mix(in srgb, var(--teal, #2e7d77) 40%, transparent);
  cursor: pointer;
}
.rule-link:hover { text-decoration-color: var(--teal, #2e7d77); }
</style>
