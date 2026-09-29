<script setup>
// The reference index list (Abilities, Armor, World, …). `state` comes from useReference().
defineProps({ state: { type: Object, required: true } });
</script>

<template>
  <ul class="ref-list">
    <li
      v-for="r in state.items"
      :key="r.mode + r.type"
      class="ref-item"
      :class="{ active: state.current?.type === r.type && state.current?.mode === r.mode }"
      @click="state.select(r)"
    >
      <span class="ref-name">{{ r.label }}</span>
      <span v-if="r.count !== null" class="ref-count">{{ r.count }}</span>
    </li>
  </ul>
</template>

<style scoped>
.ref-list { list-style: none; margin: 0; padding: 0; }
.ref-item {
  display: flex;
  align-items: center;
  padding: 5px 12px;
  cursor: pointer;
  border-left: 3px solid transparent;
}
.ref-item:hover { background: var(--hover-bg, #f5f5f5); }
.ref-item.active { background: var(--active-bg, #e8f0fe); border-left-color: var(--accent, #2d7a68); }
.ref-name { flex: 1; font-size: 0.88rem; font-weight: 500; }
.ref-count { font-size: 0.75rem; color: var(--muted); margin-left: 4px; }
</style>
