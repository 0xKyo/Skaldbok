<script setup>
// A spell, an ability, a kin...: a collapsed line that opens to its fields and text. Text is always shown as text, never as HTML.
import { computed } from 'vue';

const props = defineProps({
  entry: { type: Object, required: true },
  open: { type: Boolean, default: false },
});

const paragraphs = computed(() => (props.entry.body ?? '').split('\n').filter((p) => p.trim() !== ''));
</script>

<template>
  <details class="entry" :open="open">
    <summary>
      <span class="name">{{ entry.name }}</span>
      <span v-if="entry.subtitle" class="sub">{{ entry.subtitle }}</span>
      <span v-if="entry.source" class="chip" :class="entry.homebrew ? 'homebrew' : 'src'">{{ entry.source }}<template v-if="entry.page"> · {{ entry.page }}</template></span>
    </summary>
    <div class="body">
      <p v-if="entry.found === false" class="muted">This is no longer in the loaded content, so only its name is known.</p>
      <dl v-if="entry.fields?.length" class="fields">
        <template v-for="f in entry.fields" :key="f.label">
          <dt>{{ f.label }}</dt>
          <dd>{{ f.value }}</dd>
        </template>
      </dl>
      <p v-for="(p, i) in paragraphs" :key="i">{{ p }}</p>
    </div>
  </details>
</template>
