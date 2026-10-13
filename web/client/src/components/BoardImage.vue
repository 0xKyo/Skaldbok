<script setup>
// A picture on a board: one of the book's (an adventure's map, a portrait...) or one the GM pasted or dropped. It is fetched with the GM's token and
// fills its card.
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { apiBlobUrl } from '../api.js';
import { lang, t } from '../i18n.js';

const props = defineProps({
  token: { type: String, required: true },
  image: { type: Object, required: true },        // {adventure, file} or {id}
  alt: { type: String, default: '' },
});

const url = ref('');
const failed = ref(false);

const path = computed(() => (props.image.id
  ? `/gm/board/images/${encodeURIComponent(props.image.id)}`
  : `/gm/adventures/${encodeURIComponent(props.image.adventure)}/image?path=${encodeURIComponent(props.image.file)}`));

// a book's picture with words on it has one per language: asked again when the language of the page changes
async function load() {
  try {
    const next = await apiBlobUrl(path.value, props.token);
    if (url.value) URL.revokeObjectURL(url.value);
    url.value = next;
    failed.value = false;
  } catch {
    failed.value = !url.value;
  }
}
onMounted(load);
watch(lang, () => { if (!props.image.id) load(); });
onBeforeUnmount(() => {
  if (url.value) URL.revokeObjectURL(url.value);
});
</script>

<template>
  <img v-if="url" :src="url" :alt="alt" class="board-img" draggable="false" data-test="board-image" />
  <p v-else-if="failed" class="missing" data-test="board-image-missing">{{ t('The picture is missing.') }}</p>
</template>

<style scoped>
.board-img { display: block; width: 100%; height: 100%; object-fit: contain; pointer-events: none; }
.missing { margin: 8px; font-size: 0.8rem; color: var(--muted); }
</style>
