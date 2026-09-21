<script setup>
// A picture the GM sent. It is fetched with the player's token and shown from a blob address that is released when it goes away.
import { onBeforeUnmount, onMounted, ref } from 'vue';
import { apiBlobUrl } from '../api.js';

const props = defineProps({ token: { type: String, required: true }, path: { type: String, required: true }, alt: { type: String, default: '' } });

const url = ref('');
const failed = ref(false);

onMounted(async () => {
  try {
    url.value = await apiBlobUrl(props.path, props.token);
  } catch {
    failed.value = true;
  }
});
onBeforeUnmount(() => {
  if (url.value) URL.revokeObjectURL(url.value);
});
</script>

<template>
  <img v-if="url" :src="url" :alt="alt" class="message-image" data-test="message-image" />
  <p v-else-if="failed" class="muted small">The picture is not available.</p>
  <p v-else class="muted small">Loading the picture…</p>
</template>
