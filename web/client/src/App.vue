<script setup>
// The player's page: their own sheet (which they can edit), their party, the chat with the GM, and the rules. The token comes from
// the personal link.
import { computed, onBeforeUnmount, onMounted, ref, watchEffect } from 'vue';
import { ApiError, apiGet, apiSend, captureToken, forgetToken } from './api.js';
import ChatView from './components/ChatView.vue';
import PartyView from './components/PartyView.vue';
import RulesView from './components/RulesView.vue';
import SheetView from './components/SheetView.vue';

const POLL_MS = 3000; // a change by the GM shows up within a few seconds

const token = ref(captureToken());
const me = ref(null);
const party = ref(null);
const chat = ref({ messages: [], gmRead: '', playerRead: '' });
const problem = ref(''); // why the link does not work
const offline = ref(false);
const lastUpdate = ref(null);
const tab = ref(['sheet', 'party', 'chat', 'rules'].includes(window.location.hash.slice(1)) ? window.location.hash.slice(1) : 'sheet');
let timer = null;

// what the GM wrote after the last message this player read (the server keeps the marker, so it follows them from phone to phone)
const unread = computed(() => chat.value.messages.filter((m) => m.from === 'gm' && m.id > chat.value.playerRead).length);
const tabs = computed(() => [
  { id: 'sheet', label: 'My character' },
  { id: 'party', label: 'Party' },
  { id: 'chat', label: unread.value && tab.value !== 'chat' ? `Chat (${unread.value})` : 'Chat' },
  { id: 'rules', label: 'Rules' },
]);

// Looking at the chat is reading it.
async function markRead() {
  if (tab.value !== 'chat' || !unread.value) return;
  const newest = chat.value.messages[chat.value.messages.length - 1].id;
  chat.value = { ...chat.value, playerRead: newest };
  try {
    await apiSend('POST', '/chat/read', token.value, { upTo: newest });
  } catch {
    /* the next look tries again */
  }
}

function pick(id) {
  tab.value = id;
  markRead();
  window.history.replaceState(null, '', `${window.location.pathname}${window.location.search}#${id}`);
}

async function refresh() {
  if (!token.value) return;
  try {
    const [mine, group, thread] = await Promise.all([apiGet('/me', token.value), apiGet('/party', token.value), apiGet('/chat', token.value).catch(() => null)]);
    me.value = mine;
    party.value = group.party;
    if (thread) chat.value = thread; // an older server has no chat: the rest still works
    markRead();
    offline.value = false;
    problem.value = '';
    lastUpdate.value = new Date();
  } catch (e) {
    if (e instanceof ApiError && (e.status === 401 || e.status === 429)) {
      // an invalid link is forgotten; being blocked for a while is not a reason to forget a good one
      if (e.status === 401) {
        forgetToken();
        token.value = null;
        me.value = null;
      }
      problem.value = e.message;
    } else {
      offline.value = true; // keep showing the last data while the connection is down
    }
  }
}

function schedule() {
  clearTimeout(timer);
  timer = setTimeout(async () => {
    if (!document.hidden) await refresh();
    schedule();
  }, POLL_MS);
}

const onVisible = () => {
  if (!document.hidden) refresh();
};

onMounted(() => {
  refresh();
  schedule();
  document.addEventListener('visibilitychange', onVisible);
});
onBeforeUnmount(() => {
  clearTimeout(timer);
  document.removeEventListener('visibilitychange', onVisible);
});

const updated = computed(() => (lastUpdate.value ? lastUpdate.value.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }) : ''));
watchEffect(() => {
  document.title = me.value ? `${me.value.name} · Skaldbok` : 'Skaldbok';
});
</script>

<template>
  <main class="wrap">
    <div v-if="!token" class="notice" data-test="no-link">
      <h1>Skaldbok</h1>
      <p v-if="problem" class="error">{{ problem }}</p>
      <p v-else>Open the personal link your GM sent you. It looks like <span class="gold">…/?t=xxxxxxxx</span>.</p>
      <p class="muted small">The link is yours alone: it opens your character and nobody else's.</p>
    </div>

    <div v-else-if="!me" class="notice" data-test="loading">
      <p v-if="problem" class="error">{{ problem }}</p>
      <p v-else-if="offline" class="error">Cannot reach the server. Trying again…</p>
      <p v-else class="muted">Loading your character…</p>
    </div>

    <template v-else>
      <header class="top">
        <span class="wordmark">Skaldbok</span>
        <span class="who" data-test="who">{{ me.kin.name }} {{ me.profession.name }} · {{ me.age.label }}<span v-if="me.school"> · {{ me.school }}</span><span v-if="me.party"> · {{ me.party }}</span></span>
        <span class="live" :title="offline ? 'Connection lost: showing the last data' : 'Updates by itself every few seconds'">
          <span class="dot" :class="{ off: offline }"></span>{{ offline ? 'offline' : `updated ${updated}` }}
        </span>
      </header>
      <nav class="tabs" role="tablist">
        <button v-for="t in tabs" :key="t.id" role="tab" :aria-selected="tab === t.id" @click="pick(t.id)">{{ t.label }}</button>
      </nav>
      <SheetView v-if="tab === 'sheet'" :me="me" :token="token" @updated="(fresh) => (me = fresh)" />
      <PartyView v-else-if="tab === 'party'" :party="party" />
      <ChatView v-else-if="tab === 'chat'" :thread="chat" :token="token" @sent="refresh" />
      <RulesView v-else :token="token" />
    </template>
  </main>
</template>
