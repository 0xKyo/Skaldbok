<script setup>
// What takes the place of the hit points at 0: the death rolls, three boxes of successes and three of failures. Three successes bring the
// hit points back (the sheet does that), three failures and the character is dead.
import { t } from '../i18n.js';

const props = defineProps({
  successes: { type: Number, default: 0 },
  failures: { type: Number, default: 0 },
  editable: { type: Boolean, default: false },
});
const emit = defineEmits(['update:successes', 'update:failures']);

const BOXES = [0, 1, 2];
// tapping the last marked box clears it; any other box marks up to itself
const pick = (count, i) => (count === i + 1 ? i : i + 1);
</script>

<template>
  <div class="points hp death" data-test="death-rolls">
    <h3>{{ t('Death rolls') }}</h3>
    <div v-if="failures >= 3" class="dead" data-test="dead">
      {{ t('Dead') }}
      <button v-if="editable" type="button" class="link undo" data-test="undo-death" @click="emit('update:failures', 2)">{{ t('Undo') }}</button>
    </div>
    <div v-else class="death-rows">
      <div class="death-row" data-test="death-successes">
        <span class="what">{{ t('Success') }}</span>
        <span class="pips">
          <template v-for="i in BOXES" :key="i">
            <button v-if="editable" type="button" class="pip tap" :class="{ gone: i >= successes }" :aria-label="t('Success') + ' ' + (i + 1)" @click="emit('update:successes', pick(successes, i))"></button>
            <span v-else class="pip" :class="{ gone: i >= successes }"></span>
          </template>
        </span>
      </div>
      <div class="death-row" data-test="death-failures">
        <span class="what">{{ t('Failure') }}</span>
        <span class="pips">
          <template v-for="i in BOXES" :key="i">
            <button v-if="editable" type="button" class="pip tap" :class="{ gone: i >= failures }" :aria-label="t('Failure') + ' ' + (i + 1)" @click="emit('update:failures', pick(failures, i))"></button>
            <span v-else class="pip" :class="{ gone: i >= failures }"></span>
          </template>
        </span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.death-rows { display: flex; flex-direction: column; gap: 8px; }
.death-row { display: flex; align-items: center; gap: 12px; }
.what { min-width: 5rem; font-weight: 700; letter-spacing: 0.06em; text-transform: uppercase; font-size: 0.8rem; }
.dead { display: flex; align-items: center; gap: 14px; font-family: var(--display); font-weight: 900; font-size: 1.6rem; letter-spacing: 0.12em; text-transform: uppercase; padding: 4px 0; }
.undo { color: #fff; font-size: 0.8rem; font-weight: 600; letter-spacing: 0; text-transform: none; }
</style>
