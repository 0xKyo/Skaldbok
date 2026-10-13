<script setup>
// An entry of the Reference shown as a line that opens into its card right where it is, without leaving the page (a spell on the sheet).
// The card is the one a text includes ("{{include: …}}"); its name leads to the entry in the Reference.
import { ref } from 'vue';
import { t } from '../i18n.js';
import { cardOf } from '../lib/cards.js';
import IncludedCard from './IncludedCard.vue';

const props = defineProps({
  entry: { type: Object, required: true },        // {key, name, subtitle}
  type: { type: String, required: true },         // the Reference page of the card: "spells"
  token: { type: String, default: '' },
  prefix: { type: String, default: '' },          // '/gm' when the GM is signed in
});
const emit = defineEmits(['follow']);

const open = ref(false);
const card = ref(null);
const missing = ref(false);

async function toggle() {
  open.value = !open.value;
  if (!open.value || card.value || missing.value) return;
  card.value = await cardOf(props.prefix, props.token, props.type, props.entry.key);
  missing.value = !card.value;
}
</script>

<template>
  <div class="inline-card" data-test="inline-card">
    <button type="button" class="entry-link" :aria-expanded="open" data-test="inline-card-toggle" @click="toggle">
      <span class="entry-link-name">{{ open ? '▾' : '▸' }} {{ entry.name }}</span>
      <span v-if="entry.subtitle" class="entry-link-sub">{{ entry.subtitle }}</span>
    </button>
    <IncludedCard v-if="open && card" :card="card" :closable="false" @follow="emit('follow', $event)" />
    <p v-else-if="open && missing" class="muted small" data-test="inline-card-missing">{{ t('This entry is no longer in the loaded content; only its name is known.') }}</p>
  </div>
</template>
