<script setup>
// A picture on a board: one of the book's (an adventure's map, a portrait...) or one the GM pasted or dropped. It is fetched with the GM's token and
// fills its card.
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { apiBlobUrl } from '../api.js';

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

onMounted(async () => {
  try {
    url.value = await apiBlobUrl(path.value, props.token);
  } catch {
    failed.value = true;
  }
});
onBeforeUnmount(() => {
  if (url.value) URL.revokeObjectURL(url.value);
});
</script>

<template>
  <img v-if="url" :src="url" :alt="alt" class="board-img" draggable="false" data-test="board-image" />
  <p v-else-if="failed" class="missing" data-test="board-image-missing">The picture is missing.</p>
</template>

<style scoped>
.board-img { display: block; width: 100%; height: 100%; object-fit: contain; pointer-events: none; }
.missing { margin: 8px; font-size: 0.8rem; color: var(--muted); }
</style>
