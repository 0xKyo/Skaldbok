<script setup>
// Hit points or willpower points as on the printed sheet: the number now, and a row of circles that go empty as they are lost.
// When the sheet is being edited a circle can be tapped to set the points, and the maximum's bonus can be changed.
import { computed } from 'vue';

const props = defineProps({
  label: { type: String, required: true },
  current: { type: Number, required: true },
  max: { type: Number, required: true },
  kind: { type: String, default: 'hp' },
  editable: { type: Boolean, default: false },
  bonus: { type: Number, default: 0 },
  flagged: { type: Boolean, default: false }, // the player broke a rule here and nobody has approved it
});
const emit = defineEmits(['update:current', 'update:bonus']);

const pips = computed(() => Array.from({ length: Math.min(Math.max(props.max, 0), 30) }, (_, i) => i < props.current));
const low = computed(() => props.max > 0 && props.current * 2 <= props.max);

// tapping the last full circle empties it; any other circle fills up to itself
const setTo = (i) => emit('update:current', props.current === i + 1 ? i : i + 1);
const step = (d) => emit('update:current', Math.max(0, props.current + d));
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
    <div v-if="editable" class="points-edit">
      <button type="button" class="step" aria-label="One less" @click="step(-1)">−</button>
      <button type="button" class="step" aria-label="One more" @click="step(1)">+</button>
      <label>
        Maximum bonus
        <input type="number" :value="bonus" min="-99" max="99" data-test="bonus" @input="emit('update:bonus', Math.trunc(Number($event.target.value) || 0))" />
      </label>
    </div>
  </div>
</template>
