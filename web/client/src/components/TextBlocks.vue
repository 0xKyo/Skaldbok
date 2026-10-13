<script setup>
// A text laid out as its blocks, in reading order: paragraphs, bullet lists, the tables a "{{table: Name}}" line places and the cards a
// "{{include: Ref}}" line includes. Links in the words are followed by whoever listens to `follow`.
import DataTable from './DataTable.vue';
import IncludedCard from './IncludedCard.vue';
import RuleText from './RuleText.vue';
import { textBlocks } from '../lib/ruletables.js';

defineProps({
  text: { type: String, default: '' },
  tables: { type: Array, default: () => [] },        // the tables the text may place
  includes: { type: Array, default: () => [] },      // the cards the text may include
  intro: { type: Boolean, default: false },          // the smaller type of the intro of a page
  pinnable: { type: Boolean, default: true },        // its tables offer the pin to the board
});
const emit = defineEmits(['follow']);
</script>

<template>
  <template v-for="(b, i) in textBlocks(text, tables, includes)" :key="i">
    <IncludedCard v-if="b.card" :card="b.card" @follow="emit('follow', $event)" />
    <DataTable v-else-if="b.table" :table="b.table" :pinnable="pinnable" />
    <ul v-else-if="b.list" class="rule-list"><li v-for="(it, k) in b.list" :key="k"><RuleText :text="it" @follow="emit('follow', $event)" /></li></ul>
    <p v-else :class="intro ? 'intro-p' : 'block-p'"><RuleText :text="b.text" @follow="emit('follow', $event)" /></p>
  </template>
</template>
