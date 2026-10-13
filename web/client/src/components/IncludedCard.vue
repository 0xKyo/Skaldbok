<script setup>
// The INCLUDE pattern (see .claude/skills/linking-entities): another entity drawn inside a text ("{{include: ability_adaptive}}") or a page, as a
// card of its own that can be opened and closed; its name is a link to the entity's own page.
import { ref } from 'vue';
import { t } from '../i18n.js';
import RuleText from './RuleText.vue';

const props = defineProps({
  card: { type: Object, required: true },        // {key, name, subtitle, fields[], body}
  startOpen: { type: Boolean, default: true },
  closable: { type: Boolean, default: true },    // false where something else opens and closes it (InlineCard)
});
const open = ref(props.startOpen);
const emit = defineEmits(['follow']);
const paragraphs = (text) => (text ?? '').split('\n').filter((p) => p.trim() !== '');
</script>

<template>
  <aside class="included" data-test="included-card">
    <header>
      <button v-if="closable" type="button" class="included-toggle" :aria-expanded="open" :aria-label="open ? t('Close') : t('Open')" data-test="included-toggle" @click="open = !open">{{ open ? '▾' : '▸' }}</button>
      <button type="button" class="included-name" :title="card.key" @click="emit('follow', card.key)">{{ card.name }}</button>
      <span v-if="card.subtitle" class="included-sub">{{ card.subtitle }}</span>
    </header>
    <template v-if="open">
    <dl v-if="card.fields?.length" class="included-fields">
      <template v-for="f in card.fields" :key="f.label"><dt>{{ f.label }}</dt><dd><RuleText :text="f.value" @follow="(k) => emit('follow', k)" /></dd></template>
    </dl>
    <p v-for="(p, i) in paragraphs(card.body)" :key="i"><RuleText :text="p" @follow="(k) => emit('follow', k)" /></p>
    </template>
  </aside>
</template>

<style scoped>
.included { margin: 0.6em 0 0.9em; padding: 8px 12px; border: 1px solid var(--line); border-left: 4px solid var(--green-dark); border-radius: var(--radius); background: var(--cream); }
.included header { display: flex; flex-wrap: wrap; align-items: baseline; gap: 4px 10px; margin-bottom: 4px; }
.included-name { background: none; border: 0; padding: 0; font: inherit; font-family: var(--display); font-weight: 700; color: var(--green-dark); text-transform: uppercase; letter-spacing: 0.04em; cursor: pointer; text-decoration: underline dotted; text-underline-offset: 3px; }
.included-toggle { background: none; border: 0; padding: 0 2px; font: inherit; color: var(--green-dark); cursor: pointer; }
.included-sub { font-size: 0.8rem; color: var(--muted); }
.included-fields { display: grid; grid-template-columns: auto 1fr; gap: 2px 10px; margin: 0 0 6px; }
.included-fields dt { color: var(--muted); font-weight: 600; }
.included-fields dd { margin: 0; }
.included p { margin: 0 0 0.5em; }
.included p:last-child { margin-bottom: 0; }
</style>
