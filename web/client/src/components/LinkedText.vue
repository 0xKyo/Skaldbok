<script setup>
// A text with links in it: the words the adventure says lead to a page (its `keywords`) are links. Each page is linked once per text, and a
// text never links to the page it belongs to.
import { computed } from 'vue';

const props = defineProps({
  text: { type: String, default: '' },
  matcher: { type: Object, default: null },        // {regex, map}: the words (longest first) and the page each one leads to
  owner: { type: String, default: '' },            // the page this text is on
});
const emit = defineEmits(['go']);

const parts = computed(() => {
  const text = props.text ?? '';
  const { regex, map } = props.matcher ?? {};
  if (!regex) return [{ text }];
  const out = [];
  const used = new Set();
  let last = 0;
  for (const m of text.matchAll(regex)) {
    const target = map.get(m[0]);
    if (!target || target === props.owner || used.has(target)) continue;
    used.add(target);
    if (m.index > last) out.push({ text: text.slice(last, m.index) });
    out.push({ text: m[0], target });
    last = m.index + m[0].length;
  }
  if (last < text.length) out.push({ text: text.slice(last) });
  return out;
});
</script>

<template>
  <span class="linked"><template v-for="(p, i) in parts" :key="i"><a v-if="p.target" href="#" class="kw" data-test="kw" @click.prevent="emit('go', p.target)">{{ p.text }}</a><template v-else>{{ p.text }}</template></template></span>
</template>

<style scoped>
.kw { color: var(--green-dark); text-decoration: underline dotted; text-underline-offset: 2px; }
.kw:hover { background: var(--parchment); }
</style>
