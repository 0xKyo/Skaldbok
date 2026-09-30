<script setup>
// Index + panel side by side (stacked on narrow screens). Same pieces the GM sidebar uses.
import ReferenceIndex from './ReferenceIndex.vue';
import ReferencePanel from './ReferencePanel.vue';

defineProps({
  state: { type: Object, required: true }, // from useReference()
  token: { type: String, required: true },
  gmPrefix: { type: String, default: '' },
});
</script>

<template>
  <section class="ref-view" :class="{ 'is-open': !!state.current }" aria-label="Rules">
    <ReferenceIndex class="ref-view-index" :state="state" />
    <button v-if="state.current" type="button" class="ref-back link" data-test="reference-back" @click="state.current = null">← All categories</button>
    <ReferencePanel class="ref-view-panel" :state="state" :token="token" :gm-prefix="gmPrefix" />
  </section>
</template>

<style scoped>
.ref-view { display: grid; grid-template-columns: 1fr; gap: 12px; align-items: start; }
@media (min-width: 720px) {
  .ref-view { grid-template-columns: 190px minmax(0, 1fr); }
}
.ref-view-panel { min-width: 0; }
/* on a phone the index and a category take turns (the category used to open below the whole index, out of sight) */
.ref-back { justify-self: start; }
@media (min-width: 720px) { .ref-back { display: none; } }
@media (max-width: 719px) { .ref-view.is-open .ref-view-index { display: none; } }
</style>
