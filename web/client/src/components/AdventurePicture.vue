<script setup>
// A picture of an adventure (a map, a portrait, an illustration): fetched with the GM's token from the pack's images folder, shown small in
// the text and big, over the page, when it is clicked.
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { apiBlobUrl } from '../api.js';
import { lang, t } from '../i18n.js';
import PinButton from './PinButton.vue';

const props = defineProps({
  token: { type: String, required: true },
  adventure: { type: String, required: true },       // the id of the adventure
  file: { type: String, required: true },            // "images/maps/outskirt.jpg", as the node says
  alt: { type: String, default: '' },
});

const url = ref('');
const failed = ref(false);
const big = ref(false);

// where it goes in the page: images/<role>/...
const role = computed(() => props.file.split('/')[1] ?? 'art');

// a map with words on it has its own picture in each language: asked again when the language of the page changes
async function load() {
  try {
    const next = await apiBlobUrl(`/gm/adventures/${encodeURIComponent(props.adventure)}/image?path=${encodeURIComponent(props.file)}`, props.token);
    if (url.value) URL.revokeObjectURL(url.value);
    url.value = next;
    failed.value = false;
  } catch {
    failed.value = !url.value;
  }
}
onMounted(load);
watch(lang, load);
onBeforeUnmount(() => {
  if (url.value) URL.revokeObjectURL(url.value);
});

const close = () => { big.value = false; };

// what the 📌 button puts on the master's board: this picture
const pinPicture = () => ({ type: 'image', key: `image:${props.adventure}:${props.file}`, title: props.alt || t('Picture'), data: { adventure: props.adventure, file: props.file } });
</script>

<template>
  <figure v-if="url" class="pic" :class="role" data-test="picture">
    <img :src="url" :alt="alt" loading="lazy" @click="big = true" />
    <PinButton class="pin-corner" :snapshot="pinPicture" small />
  </figure>
  <p v-else-if="failed" class="muted pic-missing" data-test="picture-missing">{{ t('A picture is missing.') }}</p>

  <Teleport to="body">
    <div v-if="big" class="lightbox" data-test="lightbox" role="dialog" :aria-label="t('Picture')" @click="close" @keydown.esc="close">
      <img :src="url" :alt="alt" />
    </div>
  </Teleport>
</template>

<style scoped>
.pic { margin: 6px 0 10px; position: relative; }
.pin-corner { position: absolute; top: 4px; right: 4px; opacity: 0; transition: opacity 0.15s; background: rgba(251, 246, 230, 0.92); border-radius: 12px; }
.pic:hover .pin-corner, .pin-corner:focus-visible { opacity: 1; }
.pic img { display: block; width: 100%; height: auto; cursor: zoom-in; }
.pic.maps img { border: 1px solid var(--line); border-radius: 6px; }
.pic.chapters img { border-radius: 6px; }
.pic.people, .pic.art { float: right; width: 150px; margin: 2px 0 8px 14px; }
.pic.art { width: 190px; }
.pic.maps, .pic.chapters { max-width: 640px; clear: both; }
.pic-missing { font-size: 0.8rem; }
@media (max-width: 560px) { .pic.people { width: 110px; } .pic.art { width: 130px; } }
.lightbox { position: fixed; inset: 0; z-index: 50; display: flex; align-items: center; justify-content: center; padding: 16px; background: rgba(20, 16, 8, 0.82); cursor: zoom-out; }
.lightbox img { max-width: 100%; max-height: 100%; object-fit: contain; border-radius: 6px; background: #fbf6e6; }
</style>
