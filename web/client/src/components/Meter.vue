<script setup>
import { computed } from 'vue';

const props = defineProps({
  label: { type: String, required: true },
  current: { type: Number, required: true },
  max: { type: Number, required: true },
  kind: { type: String, default: 'hp' },
});

const pct = computed(() => (props.max > 0 ? Math.max(0, Math.min(100, (props.current / props.max) * 100)) : 0));
const level = computed(() => (props.kind === 'wp' ? 'wp' : pct.value <= 25 ? 'danger' : pct.value <= 50 ? 'warn' : ''));
</script>

<template>
  <div class="meter" :class="level">
    <div class="head">
      <span class="gold">{{ label }}</span>
      <span data-test="meter-value">{{ current }} / {{ max }}</span>
    </div>
    <div class="bar" role="progressbar" :aria-label="label" :aria-valuenow="current" aria-valuemin="0" :aria-valuemax="max">
      <div class="fill" :style="{ width: pct + '%' }"></div>
    </div>
  </div>
</template>
