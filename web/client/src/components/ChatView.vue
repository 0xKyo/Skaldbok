<script setup>
// The conversation with the GM. Players only ever talk to the GM; a broadcast (the GM writing to the whole party) has its own look.
// Text is always shown as text, never as HTML.
import { computed, nextTick, ref, watch } from 'vue';
import { apiSend } from '../api.js';
import MessageImage from './MessageImage.vue';

const MAX_PICTURE = 8 * 1024 * 1024;

const props = defineProps({
  thread: { type: Object, default: () => ({ messages: [], gmRead: '', playerRead: '' }) },
  token: { type: String, required: true },
});
const emit = defineEmits(['sent']);

const log = ref(null);
const text = ref('');
const picture = ref(null); // { name, data (base64) }
const sending = ref(false);
const error = ref('');

const messages = computed(() => props.thread.messages ?? []);
const canSend = computed(() => (text.value.trim() !== '' || picture.value) && !sending.value);

const timeOf = (iso) => {
  const d = new Date(iso);
  return Number.isNaN(d.getTime()) ? '' : d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
};
const dayOf = (iso) => {
  const d = new Date(iso);
  return Number.isNaN(d.getTime()) ? '' : d.toLocaleDateString([], { dateStyle: 'medium' });
};
const startsDay = (i) => i === 0 || dayOf(messages.value[i].at) !== dayOf(messages.value[i - 1].at);
const lines = (value) => (value ?? '').split('\n');
const seen = (m) => m.from === 'player' && props.thread.gmRead && m.id <= props.thread.gmRead;

// a new message: show it
watch(
  () => messages.value.length,
  async () => {
    await nextTick();
    if (log.value) log.value.scrollTop = log.value.scrollHeight;
  },
  { immediate: true },
);

function pick(event) {
  const file = event.target.files?.[0];
  event.target.value = '';
  if (!file) return;
  error.value = '';
  if (!/^image\/(png|jpeg|gif|webp)$/.test(file.type)) {
    error.value = 'Choose a PNG, JPEG, GIF or WebP picture.';
    return;
  }
  if (file.size > MAX_PICTURE) {
    error.value = 'That picture is too big (8 MB at most).';
    return;
  }
  const reader = new FileReader();
  reader.onload = () => {
    picture.value = { name: file.name, data: String(reader.result).split(',')[1] ?? '' };
  };
  reader.readAsDataURL(file);
}

async function send() {
  if (!canSend.value) return;
  sending.value = true;
  error.value = '';
  try {
    const body = {};
    if (text.value.trim()) body.text = text.value.trim();
    if (picture.value) body.image = picture.value.data;
    await apiSend('POST', '/chat', props.token, body);
    text.value = '';
    picture.value = null;
    emit('sent');
  } catch (e) {
    error.value = e.message;
  } finally {
    sending.value = false;
  }
}
</script>

<template>
  <section class="chat" aria-label="Chat with your GM">
    <div ref="log" class="chat-log" data-test="chat-log">
      <p v-if="!messages.length" class="muted">Nothing yet. Write to your GM below; only they can read it.</p>
      <template v-for="(m, i) in messages" :key="m.id">
        <div v-if="startsDay(i)" class="chat-day muted small">{{ dayOf(m.at) }}</div>
        <article v-if="m.kind === 'broadcast'" class="broadcast" data-test="broadcast">
          <header><b>Broadcast</b><span v-if="m.to"> · {{ m.to }}</span><span class="muted small"> · {{ timeOf(m.at) }}</span></header>
          <MessageImage v-if="m.image" :token="token" :path="m.image" :alt="m.text" />
          <p v-for="(line, n) in lines(m.text)" :key="n" class="message-text">{{ line }}</p>
        </article>
        <div v-else class="bubble" :class="m.from === 'player' ? 'mine' : 'theirs'" data-test="message">
          <MessageImage v-if="m.image" :token="token" :path="m.image" :alt="m.text" />
          <p v-for="(line, n) in lines(m.text)" :key="n" class="message-text">{{ line }}</p>
          <small>{{ timeOf(m.at) }}<span v-if="seen(m)" data-test="seen"> · seen</span></small>
        </div>
      </template>
    </div>

    <form class="composer" @submit.prevent="send">
      <textarea v-model="text" rows="2" maxlength="4000" placeholder="Write to your GM…" aria-label="Message" @keydown.enter.ctrl.prevent="send"></textarea>
      <div class="composer-row">
        <label class="btn secondary">
          Picture
          <input type="file" accept="image/png,image/jpeg,image/gif,image/webp" hidden @change="pick" />
        </label>
        <span v-if="picture" class="small muted" data-test="attached">{{ picture.name }} <button type="button" class="link" @click="picture = null">remove</button></span>
        <button type="submit" class="btn" :disabled="!canSend" data-test="send">Send</button>
      </div>
      <p v-if="error" class="error small" data-test="chat-error">{{ error }}</p>
    </form>
  </section>
</template>
