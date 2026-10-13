<script setup>
// A whole number the user types. What is typed is left alone while they type (they may clear the box to write another number): the number
// is checked, kept between `min` and `max` and handed over only when they finish, leaving the box (or pressing Enter).
import { ref } from 'vue';

const props = defineProps({
  modelValue: { type: Number, default: 0 },
  min: { type: Number, default: 0 },
  max: { type: Number, default: 99 },
  blankZero: { type: Boolean, default: false },   // a box that holds 0 is shown empty (nothing written yet)
});
const emit = defineEmits(['update:modelValue']);

const typed = ref(null);                    // what is in the box while it is being written; null when it shows the number

function finish(event) {
  const value = Math.min(props.max, Math.max(props.min, Math.trunc(Number(typed.value ?? event.target.value) || 0)));
  typed.value = null;
  event.target.value = props.blankZero && value === 0 ? '' : value;               // (the number may be the one it already was: nothing else would redraw the box)
  emit('update:modelValue', value);
}
</script>

<template>
  <input type="number" :min="min" :max="max" :value="typed ?? (blankZero && modelValue === 0 ? '' : modelValue)" @input="typed = $event.target.value" @change="finish" @blur="typed !== null && finish($event)" />
</template>
