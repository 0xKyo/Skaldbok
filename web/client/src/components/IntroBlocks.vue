<script setup>
// The intro of a page: its text, its named sections and the tables the text did not place.
import { computed } from 'vue';
import DataTable from './DataTable.vue';
import TextBlocks from './TextBlocks.vue';
import { unplacedTables } from '../lib/ruletables.js';

const props = defineProps({
  intro: { type: Object, required: true },           // {body, sections: [{title, body}], tables}
});
const emit = defineEmits(['follow']);

const sections = computed(() => props.intro.sections ?? []);
const loose = computed(() => unplacedTables([props.intro.body, ...sections.value.map((s) => s.body)], props.intro.tables ?? []));
</script>

<template>
  <TextBlocks :text="intro.body" :tables="intro.tables" intro @follow="emit('follow', $event)" />
  <template v-for="(s, si) in sections" :key="si">
    <p class="intro-section-name">{{ s.title }}</p>
    <TextBlocks :text="s.body" :tables="intro.tables" intro @follow="emit('follow', $event)" />
  </template>
  <DataTable v-for="tb in loose" :key="tb.title" :table="tb" />
</template>
