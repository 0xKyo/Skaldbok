<script setup>
import { computed } from 'vue';

const props = defineProps({
  label: { type: String, required: true },
  current: { type: Number, required: true },
  max: { type: Number, required: true },
  kind: { type: String, default: 'hp' },
  editable: { type: Boolean, default: false },
  flagged: { type: Boolean, default: false },
});
const emit = defineEmits(['update:current']);

const pips = computed(() => Array.from({ length: Math.min(Math.max(props.max, 0), 30) }, (_, i) => i < props.current));
const low = computed(() => props.max > 0 && props.current * 2 <= props.max);

// tapping the last full circle empties it; any other circle fills up to itself
const setTo = (i) => emit('update:current', props.current === i + 1 ? i : i + 1);
</script>

<template>
  <div class="points" :class="[kind, { flagged }]">
    <h3>{{ label }}<span v-if="flagged" class="flag-note"> · check the rules</span></h3>
    <div class="body">
      <div class="now" data-test="meter-value">{{ current }}<small> / {{ max }}</small></div>
      <div class="pips" role="img" :aria-label="`${label}: ${current} of ${max}`">
        <template v-for="(on, i) in pips" :key="i">
          <button v-if="editable" type="button" class="pip tap" :class="{ gone: !on, low }" :aria-label="`Set to ${i + 1}`" @click="setTo(i)"></button>
          <span v-else class="pip" :class="{ gone: !on, low }"></span>
        </template>
      </div>
    </div>
  </div>
</template>
