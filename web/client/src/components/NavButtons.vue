<script setup>
// ◀ ▶ in the header: back and forward between what has been looked at (the tabs, the Reference pages, the adventure pages).
import { inject } from 'vue';

const nav = inject('nav', null);
</script>

<template>
  <span v-if="nav" class="nav-buttons" role="group" aria-label="Navigation">
    <button type="button" class="nav-btn" data-test="nav-back" :disabled="!nav.canBack.value" title="Back" aria-label="Back" @click="nav.back()">◀</button>
    <button type="button" class="nav-btn" data-test="nav-forward" :disabled="!nav.canForward.value" title="Forward" aria-label="Forward" @click="nav.forward()">▶</button>
  </span>
</template>

<style scoped>
.nav-buttons { display: inline-flex; gap: 4px; align-self: center; }
.nav-btn { width: 30px; height: 26px; padding: 0; border: 1px solid var(--green-dark); border-radius: 14px; background: none; color: var(--green-dark); font-size: 0.7rem; line-height: 1; cursor: pointer; }
.nav-btn:hover:not(:disabled) { background: var(--green-dark); color: #f3ead2; }
.nav-btn:disabled { opacity: 0.35; cursor: default; }
</style>
