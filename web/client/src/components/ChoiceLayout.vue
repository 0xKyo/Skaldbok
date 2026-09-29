<script setup>
// A choice made the way the Reference shows a category: the options as a list on the left, and the chosen one's data on the right
// (fields, text and its tables, from the same content the Reference reads). The extra controls of a step go in the slot, under the data.
import { computed } from 'vue';
import DataTable from './DataTable.vue';
import RuleText from './RuleText.vue';
import { textBlocks } from '../lib/ruletables.js';

const props = defineProps({
  rows: { type: Array, required: true },            // [{key, title, sub}]
  modelValue: { type: String, default: '' },        // the key of the chosen row
  detail: { type: Object, default: null },          // a Reference entry: {name|title, pack, subtitle, fields[], body, tables[], sections[]}
});
const emit = defineEmits(['update:modelValue']);

const shown = computed(() => props.detail ?? props.rows.find((r) => r.key === props.modelValue) ?? null);
const heading = computed(() => shown.value?.name ?? shown.value?.title ?? '');
</script>

<template>
  <div class="choice-layout" data-test="choice-layout">
    <nav class="rules-list" aria-label="Options">
      <button v-for="r in rows" :key="r.key" type="button" class="rules-row" :class="{ active: modelValue === r.key }" data-test="choice-row" @click="emit('update:modelValue', r.key)">
        <span class="rules-row-name">{{ r.title }}</span>
        <span v-if="r.sub" class="rules-row-sub">{{ r.sub }}</span>
      </button>
    </nav>

    <article v-if="modelValue && shown" class="rules-detail panel" data-test="choice-detail">
      <div class="rules-detail-head">
        <h2 class="rules-detail-name">{{ heading }}</h2>
        <div class="chips" style="margin-top: 4px;">
          <span v-if="shown.pack" class="chip" :class="{ homebrew: shown.homebrew }">Source · {{ shown.pack }}</span>
          <span v-if="shown.subtitle" class="chip">{{ shown.subtitle }}</span>
        </div>
      </div>
      <dl v-if="shown.fields?.length" class="fields" style="margin: 10px 0;">
        <template v-for="f in shown.fields" :key="f.label"><dt>{{ f.label }}</dt><dd>{{ f.value }}</dd></template>
      </dl>
      <template v-for="(b, i) in textBlocks(shown.body ?? '', shown.tables ?? [])" :key="i">
        <DataTable v-if="b.table" :table="b.table" />
        <ul v-else-if="b.list" class="rule-list"><li v-for="(it, k) in b.list" :key="k"><RuleText :text="it" /></li></ul>
        <p v-else style="margin: 0 0 0.6em;"><RuleText :text="b.text" /></p>
      </template>
      <template v-for="(s, si) in shown.sections ?? []" :key="si">
        <h3 class="rules-section-title">{{ s.title }}</h3>
        <template v-for="(b, pi) in textBlocks(s.body, shown.tables ?? [])" :key="pi">
          <DataTable v-if="b.table" :table="b.table" />
          <p v-else-if="!b.list" style="margin: 0 0 0.6em;"><RuleText :text="b.text" /></p>
        </template>
      </template>
      <slot />
    </article>
    <p v-else class="muted pick-one">Pick one from the list.</p>
  </div>
</template>

<style scoped>
.choice-layout { display: grid; grid-template-columns: 1fr; gap: 10px; margin-top: 10px; }
@media (min-width: 640px) { .choice-layout { grid-template-columns: minmax(200px, 300px) 1fr; align-items: start; } }
.rules-list { border: 1px solid var(--line); border-radius: var(--radius); background: var(--cream); overflow: hidden; position: sticky; top: 0; max-height: calc(100vh - 12rem); overflow-y: auto; }
.rules-detail { max-height: calc(100vh - 12rem); overflow-y: auto; margin-bottom: 0; }
.rules-row { display: flex; justify-content: space-between; align-items: baseline; gap: 6px; width: 100%; padding: 9px 12px; border: 0; border-bottom: 1px solid var(--line); background: none; font: inherit; color: var(--ink); cursor: pointer; text-align: left; }
.rules-row:last-child { border-bottom: 0; }
.rules-row:hover { background: var(--parchment); }
.rules-row.active { background: var(--green-dark); color: #f3ead2; }
.rules-row.active .rules-row-sub { color: rgba(243, 234, 210, 0.7); }
.rules-row-name { font-weight: 600; }
.rules-row-sub { font-size: 0.8rem; color: var(--muted); flex-shrink: 0; }
.rules-detail-head { margin-bottom: 8px; }
.rules-detail-name { margin: 0; font-size: 1.1rem; color: var(--green-dark); text-transform: uppercase; letter-spacing: 0.04em; font-family: var(--display); }
.rules-section-title { font-size: 0.95rem; color: var(--brown); margin: 1rem 0 0.4rem; text-transform: uppercase; font-family: var(--display); letter-spacing: 0.03em; }
.pick-one { padding: 8px 4px; }
</style>
