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
  <section class="ref-view" aria-label="Rules">
    <ReferenceIndex class="ref-view-index" :state="state" />
    <ReferencePanel class="ref-view-panel" :state="state" :token="token" :gm-prefix="gmPrefix" />
  </section>
</template>

<style scoped>
.ref-view { display: grid; grid-template-columns: 1fr; gap: 12px; align-items: start; }
@media (min-width: 720px) {
  .ref-view { grid-template-columns: 190px minmax(0, 1fr); }
}
.ref-view-panel { min-width: 0; }
</style>
