<script setup>
// The open reference entry: optional tabs (e.g. "All skills / General Info", "General / Weapons / Armor"), and the list+detail view.
import { computed } from 'vue';
import CreatureView from './CreatureView.vue';
import RulesView from './RulesView.vue';

const props = defineProps({
  state: { type: Object, required: true }, // from useReference()
  token: { type: String, required: true },
  gmPrefix: { type: String, default: '' },
});

const tab = computed(() => props.state.current?.tabs[props.state.current.tab] ?? null);
const mode = computed(() => tab.value?.mode ?? props.state.current?.mode);
const type = computed(() => tab.value?.type ?? props.state.current?.type);
</script>

<template>
  <div v-if="state.current" class="ref-panel">
    <div v-if="state.current.tabs.length > 1" class="subtabs" role="group" aria-label="View">
      <button v-for="(t, i) in state.current.tabs" :key="t.type + t.mode" :aria-pressed="state.current.tab === i" @click="state.current.tab = i">
        {{ t.label }}
      </button>
    </div>
    <CreatureView v-if="type === 'creatures'" :token="token" :prefix="gmPrefix" />
    <RulesView
      v-else
      :key="type + mode"
      :token="token"
      :mode="mode"
      :jump-to="state.jump"
      :gm-prefix="gmPrefix"
      hide-tabs
      @follow-key="(key) => state.goTo({ key })"
      @seen="(key) => state.seen(key)"
    />
  </div>
  <p v-else class="muted" style="padding: 1rem;">Pick something from the index.</p>
</template>

<style scoped>
.muted { color: var(--muted); }
</style>
