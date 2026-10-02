<script setup>
// App root: detects GM vs player mode from the URL token (?gm=... or ?t=...) and renders the matching view.
import { computed, onBeforeUnmount, onMounted, provide, ref, watch } from 'vue';
import { ApiError, apiGet, apiSend, captureToken, captureGmToken, forgetToken } from './api.js';
import ChatView from './components/ChatView.vue';
import GmView from './components/GmView.vue';
import BackupMenu from './components/BackupMenu.vue';
import NavButtons from './components/NavButtons.vue';
import ReferenceView from './components/ReferenceView.vue';
import SheetView from './components/SheetView.vue';
import { createNav } from './nav.js';
import { useReference } from './reference.js';

const POLL_MS = 3000; // a change by the GM shows up within a few seconds

const gmToken = ref(captureGmToken());
const token = ref(captureToken());
const me = ref(null);
const chat = ref({ messages: [], gmRead: '', playerRead: '' });
const problem = ref(''); // why the link does not work
const offline = ref(false);
const lastUpdate = ref(null);
const tab = ref(['sheet', 'chat', 'rules'].includes(window.location.hash.slice(1)) ? window.location.hash.slice(1) : 'sheet');
const reference = useReference(() => token.value); // shared with the GM view; kept here so it survives tab switches
let timer = null;
const nav = createNav();            // back and forward between what was looked at; the parts of the app report to it
provide('nav', nav);

// what the GM wrote after the last message this player read (the server keeps the marker, so it follows them from phone to phone)
const unread = computed(() => chat.value.messages.filter((m) => m.from === 'gm' && m.id > chat.value.playerRead).length);
const tabs = computed(() => [
  { id: 'sheet', label: 'My character' },
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

function pick(id, replace = false) {
  tab.value = id;
  markRead();
  const url = `${window.location.pathname}${window.location.search}#${id}`;
  if (replace) window.history.replaceState(null, '', url);
  else window.history.pushState(null, '', url);
}

// the player's tab is one of the things the ◀ ▶ buttons remember (the GM's is kept by GmView)
watch(tab, (t) => { if (!gmToken.value) nav.record('tab', t); }, { immediate: true });
watch(() => nav.tick.value, () => {
  const t = nav.slice('tab');
  if (!gmToken.value && t && t !== tab.value) pick(t, true);
});

const onPopState = () => {
  const id = window.location.hash.slice(1);
  if (['sheet', 'chat', 'rules'].includes(id)) tab.value = id;
};

function goToRules(target) {
  reference.goTo(target);
  pick('rules');
}

async function refresh() {
  if (!token.value) return;
  try {
    const [mine, thread] = await Promise.all([apiGet('/me', token.value), apiGet('/chat', token.value).catch(() => null)]);
    me.value = mine;
    if (thread) chat.value = thread; // an older server has no chat: the rest still works
    if (!reference.items.length) reference.load().catch(() => {});
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
  // replace the current history entry so the initial tab is a real entry browsers can return to
  window.history.replaceState(null, '', `${window.location.pathname}${window.location.search}#${tab.value}`);
  refresh();
  schedule();
  document.addEventListener('visibilitychange', onVisible);
  window.addEventListener('popstate', onPopState);
});
onBeforeUnmount(() => {
  clearTimeout(timer);
  document.removeEventListener('visibilitychange', onVisible);
  window.removeEventListener('popstate', onPopState);
});

const updated = computed(() => (lastUpdate.value ? lastUpdate.value.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }) : ''));
document.title = 'Skaldbok';                         // the tab is always called Skaldbok
</script>

<template>
  <!-- GM view: full interface when accessed with the GM link -->
  <main v-if="gmToken" class="wrap gm-wrap">
    <header class="top gm-top">
      <span class="wordmark">Skaldbok <small class="ver">0.8</small></span>
      <NavButtons />
      <div id="gm-tabs" class="gm-tabs"></div>
      <BackupMenu :token="gmToken" />
      <span class="live"><span class="dot"></span>GM</span>
    </header>
    <GmView :token="gmToken" />
  </main>

  <main v-else class="wrap">
    <div v-if="!token" class="notice" data-test="no-link">
      <h1>Skaldbok <small class="ver">0.8</small></h1>
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
      <header class="top gm-top">
        <span class="wordmark">Skaldbok <small class="ver">0.8</small></span>
        <NavButtons />
        <div class="gm-tabs">
          <nav class="tabs" role="tablist">
            <button v-for="t in tabs" :key="t.id" role="tab" :aria-selected="tab === t.id" @click="pick(t.id)">{{ t.label }}</button>
          </nav>
        </div>
        <span class="live" :title="offline ? 'Connection lost: showing the last data' : 'Updates by itself every few seconds'">
          <span class="dot" :class="{ off: offline }"></span>{{ offline ? 'offline' : `updated ${updated}` }}
        </span>
      </header>
      <SheetView v-if="tab === 'sheet'" :me="me" :token="token" @updated="(fresh) => (me = fresh)" @goto-rules="goToRules" />
      <ChatView v-else-if="tab === 'chat'" :thread="chat" :token="token" @sent="refresh" />
      <ReferenceView v-else-if="tab === 'rules'" :state="reference" :token="token" />
    </template>
  </main>
</template>
