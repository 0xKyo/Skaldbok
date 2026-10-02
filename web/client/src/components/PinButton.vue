<script setup>
// 📌 Pin: puts what is on screen on the master's board (the Master tab). Only the GM has a board, so for everybody else it is not there.
// `snapshot` gives {type, key, title, data} when it is clicked (a function: what is pinned is whatever is open at that moment).
import { inject } from 'vue';

const props = defineProps({
  snapshot: { type: Function, required: true },
  small: { type: Boolean, default: false },
});

const board = inject('board', null);

function pin() {
  const what = props.snapshot();
  if (what) board.pin(what);
}
</script>

<template>
  <button v-if="board" type="button" class="pin-btn" :class="{ small }" data-test="pin" title="Pin to the Master board" aria-label="Pin to the Master board" @click.stop="pin">
    📌<span v-if="!small"> Pin</span>
  </button>
</template>

<style scoped>
.pin-btn { padding: 2px 12px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.8rem; font-weight: 600; cursor: pointer; white-space: nowrap; }
.pin-btn:hover { background: var(--green-dark); color: #f3ead2; }
.pin-btn.small { padding: 0 6px; font-size: 0.75rem; line-height: 1.4; }
</style>
