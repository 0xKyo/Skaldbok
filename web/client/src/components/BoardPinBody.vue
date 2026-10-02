<script setup>
// What a pinned thing shows on the board, by what it is: a rule or a card (text, fields, tables), a table, a creature (its stat block) or a page
// of an adventure. It is a copy of what was on screen when it was pinned, so the board does not depend on opening anything again.
import { computed } from 'vue';
import DataTable from './DataTable.vue';
import RuleText from './RuleText.vue';
import { textBlocks, unplacedTables } from '../lib/ruletables.js';

const props = defineProps({
  pin: { type: Object, required: true },        // {type, key, title, data}
});

const data = computed(() => props.pin.data ?? {});
const sections = computed(() => data.value.sections ?? []);
const tables = computed(() => data.value.tables ?? []);
const blocks = (text) => textBlocks(text ?? '', tables.value);
const loose = computed(() => unplacedTables([data.value.body, ...sections.value.map((s) => s.body)], tables.value));
</script>

<template>
  <div class="pin-body" :data-type="pin.type">
    <DataTable v-if="pin.type === 'table'" :table="data" :pinnable="false" hide-title />

    <template v-else-if="pin.type === 'creature'">
      <p v-if="data.kind || data.category" class="muted small">{{ [data.kind, data.category].filter(Boolean).join(' · ') }}</p>
      <template v-for="(b, i) in blocks(data.intro)" :key="`i${i}`">
        <ul v-if="b.list" class="bullets"><li v-for="(it, k) in b.list" :key="k">{{ it }}</li></ul>
        <p v-else-if="b.text">{{ b.text }}</p>
      </template>
      <div v-for="(block, bi) in data.blocks ?? []" :key="bi" class="stat-block">
        <h4 v-if="block.variant">{{ block.variant }}</h4>
        <table class="fields-table"><tbody><tr v-for="f in block.fields" :key="f.label"><th>{{ f.label }}</th><td>{{ f.value }}</td></tr></tbody></table>
      </div>
      <table v-if="data.attacks?.length" class="attacks-table">
        <thead><tr><th>Roll</th><th>Attack</th><th>Effect</th></tr></thead>
        <tbody><tr v-for="(a, ai) in data.attacks" :key="ai"><td>{{ a.rollText || `${a.rollMin}–${a.rollMax}` }}</td><td>{{ a.name }}</td><td>{{ a.text }}</td></tr></tbody>
      </table>
      <p v-for="a in data.abilities ?? []" :key="a.name" class="ability"><b>{{ a.name }}.</b> {{ a.text }}</p>
      <p v-if="data.description" class="muted">{{ data.description }}</p>
    </template>

    <template v-else>
      <dl v-if="data.fields?.length" class="fields">
        <template v-for="f in data.fields" :key="f.label"><dt>{{ f.label }}</dt><dd>{{ f.value }}</dd></template>
      </dl>
      <template v-for="(b, i) in blocks(data.body)" :key="`b${i}`">
        <DataTable v-if="b.table" :table="b.table" :pinnable="false" />
        <ul v-else-if="b.list" class="bullets"><li v-for="(it, k) in b.list" :key="k"><RuleText :text="it" /></li></ul>
        <p v-else><RuleText :text="b.text" /></p>
      </template>
      <template v-for="(s, si) in sections" :key="`s${si}`">
        <h4 class="sec">{{ s.title }}</h4>
        <template v-for="(b, pi) in blocks(s.body)" :key="pi">
          <DataTable v-if="b.table" :table="b.table" :pinnable="false" />
          <ul v-else-if="b.list" class="bullets"><li v-for="(it, k) in b.list" :key="k"><RuleText :text="it" /></li></ul>
          <p v-else><RuleText :text="b.text" /></p>
        </template>
      </template>
      <DataTable v-for="t in loose" :key="t.title" :table="t" :pinnable="false" />
      <p v-if="data.creatures?.length" class="creatures"><span v-for="c in data.creatures" :key="c" class="chip">{{ c }}</span></p>
    </template>
  </div>
</template>

<style scoped>
.pin-body { font-size: 0.85rem; line-height: 1.45; }
.pin-body p { margin: 0 0 0.5em; }
.bullets { margin: 4px 0 8px; padding-left: 1.1rem; }
.sec { margin: 10px 0 4px; font-size: 0.78rem; letter-spacing: 0.06em; text-transform: uppercase; color: var(--brown); }
.muted { color: var(--muted); }
.small { font-size: 0.78rem; }
.fields { display: grid; grid-template-columns: auto 1fr; gap: 2px 10px; margin: 0 0 8px; }
.fields dt { color: var(--muted); font-weight: 600; }
.fields dd { margin: 0; }
.fields-table, .attacks-table { border-collapse: collapse; font-size: 0.8rem; margin: 4px 0 8px; }
.fields-table th { text-align: left; padding: 1px 10px 1px 0; color: var(--muted); font-weight: 600; }
.attacks-table th, .attacks-table td { text-align: left; padding: 2px 8px 2px 0; vertical-align: top; }
.attacks-table th { color: var(--muted); }
.chip { display: inline-block; padding: 0 8px; border: 1px solid var(--green-dark); border-radius: 12px; font-size: 0.75rem; color: var(--green-dark); margin-right: 4px; }
</style>
