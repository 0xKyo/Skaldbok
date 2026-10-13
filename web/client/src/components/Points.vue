<script setup>
import { computed } from 'vue';
import { t } from '../i18n.js';

const props = defineProps({
  label: { type: String, required: true },
  current: { type: Number, required: true },
  max: { type: Number, required: true },
  kind: { type: String, default: 'hp' },
  editable: { type: Boolean, default: false },
  flagged: { type: Boolean, default: false },
});
const emit = defineEmits(['update:current']);

// the circles are marked as points are lost, like the printed sheet: all empty when the character is whole
const lost = computed(() => Math.max(0, props.max - props.current));
const pips = computed(() => Array.from({ length: Math.min(Math.max(props.max, 0), 30) }, (_, i) => i < lost.value));

// tapping the last marked circle clears it; any other circle marks up to itself
const setTo = (i) => emit('update:current', props.max - (lost.value === i + 1 ? i : i + 1));
</script>

<template>
  <div class="points" :class="[kind, { flagged }]">
    <h3>{{ label }}<span v-if="flagged" class="flag-note"> · {{ t('check the rules') }}</span></h3>
    <div class="body">
      <div class="now" data-test="meter-value">{{ current }}<small> / {{ max }}</small></div>
      <div class="pips" role="img" :aria-label="t('{label}: {n} of {max}', { label, n: current, max })">
        <template v-for="(on, i) in pips" :key="i">
          <button v-if="editable" type="button" class="pip tap" :class="{ gone: !on }" :aria-label="t('Set to {n}', { n: max - i - 1 })" @click="setTo(i)"></button>
          <span v-else class="pip" :class="{ gone: !on }"></span>
        </template>
      </div>
    </div>
  </div>
</template>
