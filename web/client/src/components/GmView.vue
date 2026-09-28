<script setup>
// GM interface: character list on the left, selected character's sheet/chat/rules on the right.
// Reuses SheetView, ChatView and RulesView from the player side.
import { computed, onBeforeUnmount, onMounted, ref, watchEffect } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import ChatView from './ChatView.vue';
import RulesView from './RulesView.vue';
import SheetView from './SheetView.vue';

const POLL_MS = 3000;

const props = defineProps({
  token: { type: String, required: true },
});

// ---- state ----
const characters = ref([]);     // [{id, name, player, kin, profession, age, hp, wp, conditions, link}]
const parties = ref([]);
const threads = ref([]);        // [{characterId, name, unread, last}]
const selectedId = ref(null);
const sheet = ref(null);        // full character view for the selected character
const chat = ref({ messages: [], gmRead: '', playerRead: '' });
const offline = ref(false);
const problem = ref('');
const tab = ref('sheet');
const rulesJump = ref(null);
const newCharName = ref('');
const newPartyName = ref('');
const showNewChar = ref(false);
const showNewParty = ref(false);
let timer = null;

const selected = computed(() => characters.value.find((c) => c.id === selectedId.value) ?? null);
const totalUnread = computed(() => threads.value.reduce((n, t) => n + t.unread, 0));

const gmTabs = computed(() => [
  { id: 'sheet', label: 'Sheet' },
  { id: 'chat', label: threads.value.find((t) => t.characterId === selectedId.value)?.unread > 0 && tab.value !== 'chat'
      ? `Chat (${threads.value.find((t) => t.characterId === selectedId.value).unread})` : 'Chat' },
  { id: 'tables', label: 'Tables' },
  { id: 'rules', label: 'Rules' },
]);

// ---- data loading ----
async function loadCharacterList() {
  const data = await apiGet('/gm/characters', props.token);
  characters.value = (data.characters ?? []).sort((a, b) => a.name.localeCompare(b.name));
}

async function loadSelected() {
  if (!selectedId.value) return;
  const [sheetData, chatData] = await Promise.all([
    apiGet(`/gm/characters/${selectedId.value}`, props.token),
    apiGet(`/gm/chat/${selectedId.value}`, props.token).catch(() => null),
  ]);
  sheet.value = sheetData;
  if (chatData) chat.value = chatData;
}

async function loadThreadSummaries() {
  const data = await apiGet('/gm/chat', props.token);
  threads.value = data.threads ?? [];
}

async function refresh() {
  try {
    await Promise.all([loadCharacterList(), loadThreadSummaries()]);
    if (selectedId.value) await loadSelected();
    offline.value = false;
    problem.value = '';
  } catch (e) {
    if (e instanceof ApiError && e.status === 401) {
      problem.value = e.message;
    } else {
      offline.value = true;
    }
  }
}

async function selectCharacter(id) {
  selectedId.value = id;
  tab.value = 'sheet';
  sheet.value = null;
  chat.value = { messages: [], gmRead: '', playerRead: '' };
  await loadSelected();
}

// ---- actions ----
async function createCharacter() {
  if (!newCharName.value.trim()) return;
  try {
    await apiSend('POST', '/gm/characters', props.token, { name: newCharName.value.trim() });
    newCharName.value = '';
    showNewChar.value = false;
    await refresh();
  } catch {
    /* show inline error later */
  }
}

async function createParty() {
  if (!newPartyName.value.trim()) return;
  try {
    await apiSend('POST', '/gm/parties', props.token, { name: newPartyName.value.trim() });
    newPartyName.value = '';
    showNewParty.value = false;
    const data = await apiGet('/gm/parties', props.token);
    parties.value = data.parties ?? [];
  } catch {
    /* inline later */
  }
}

function pick(id) {
  tab.value = id;
}

function goToRules(target) {
  rulesJump.value = target;
  pick('tables');
}

function followKey(key) {
  if (tab.value === 'rules') {
    rulesJump.value = { key, type: '' };
    pick('tables');
  } else {
    pick('rules');
  }
}

function schedule() {
  clearTimeout(timer);
  timer = setTimeout(async () => {
    if (!document.hidden) await refresh();
    schedule();
  }, POLL_MS);
}

onMounted(async () => {
  await refresh();
  // pre-select the first character if none selected
  if (!selectedId.value && characters.value.length) selectCharacter(characters.value[0].id);
  schedule();
});
onBeforeUnmount(() => clearTimeout(timer));

watchEffect(() => {
  document.title = selected.value ? `GM · ${selected.value.name} · Skaldbok` : 'GM · Skaldbok';
});
</script>

<template>
  <div class="gm-layout">
    <!-- ---- left sidebar: character list ---- -->
    <aside class="gm-sidebar">
      <div class="sidebar-head">
        <span class="sidebar-title">Characters</span>
        <button class="icon-btn" title="New character" @click="showNewChar = !showNewChar">+</button>
      </div>

      <form v-if="showNewChar" class="inline-form" @submit.prevent="createCharacter">
        <input v-model="newCharName" placeholder="Name…" class="inline-input" autofocus />
        <button type="submit" class="btn-sm">Create</button>
        <button type="button" class="btn-sm muted" @click="showNewChar = false">Cancel</button>
      </form>

      <ul class="char-list">
        <li
          v-for="c in characters"
          :key="c.id"
          class="char-item"
          :class="{ active: c.id === selectedId }"
          @click="selectCharacter(c.id)"
        >
          <span class="char-name">{{ c.name || c.id }}</span>
          <span class="char-meta">{{ c.kin }} {{ c.profession }}</span>
          <span v-if="threads.find((t) => t.characterId === c.id)?.unread" class="badge">
            {{ threads.find((t) => t.characterId === c.id).unread }}
          </span>
        </li>
      </ul>

      <div class="sidebar-head" style="margin-top: 1rem;">
        <span class="sidebar-title">Parties</span>
        <button class="icon-btn" title="New party" @click="showNewParty = !showNewParty">+</button>
      </div>

      <form v-if="showNewParty" class="inline-form" @submit.prevent="createParty">
        <input v-model="newPartyName" placeholder="Party name…" class="inline-input" />
        <button type="submit" class="btn-sm">Create</button>
        <button type="button" class="btn-sm muted" @click="showNewParty = false">Cancel</button>
      </form>

      <ul class="char-list">
        <li v-for="p in parties" :key="p.id" class="char-item">
          <span class="char-name">{{ p.name }}</span>
          <span class="char-meta">{{ p.members.length }} member{{ p.members.length !== 1 ? 's' : '' }}</span>
        </li>
      </ul>
    </aside>

    <!-- ---- main area ---- -->
    <div class="gm-main">
      <div v-if="!selected" class="gm-empty">
        <p v-if="problem" class="error">{{ problem }}</p>
        <p v-else-if="offline" class="error">Cannot reach the server. Trying again…</p>
        <p v-else-if="characters.length === 0" class="muted">No characters yet. Create one with the + button.</p>
        <p v-else class="muted">Select a character on the left.</p>
      </div>

      <template v-else>
        <div class="gm-char-header">
          <strong>{{ selected.name }}</strong>
          <span class="char-meta">{{ selected.kin }} {{ selected.profession }} · {{ selected.age }}</span>
          <span v-if="selected.conditions.length" class="conditions">
            {{ selected.conditions.join(', ') }}
          </span>
          <span class="hp-wp">HP {{ selected.hp.current }}/{{ selected.hp.max }} · WP {{ selected.wp.current }}/{{ selected.wp.max }}</span>
          <a v-if="selected.link" :href="selected.link" target="_blank" class="player-link">Player link ↗</a>
        </div>

        <nav class="tabs" role="tablist">
          <button v-for="t in gmTabs" :key="t.id" role="tab" :aria-selected="tab === t.id" @click="pick(t.id)">{{ t.label }}</button>
        </nav>

        <SheetView
          v-if="tab === 'sheet' && sheet"
          :me="sheet"
          :token="token"
          :patch-path="`/gm/characters/${selectedId}`"
          @updated="(fresh) => { sheet = fresh; loadCharacterList(); }"
          @goto-rules="goToRules"
        />
        <div v-else-if="tab === 'sheet'" class="muted" style="padding: 1rem;">Loading…</div>

        <ChatView
          v-else-if="tab === 'chat'"
          :thread="chat"
          :token="token"
          :send-path="`/gm/chat/${selectedId}`"
          :read-path="`/gm/chat/${selectedId}/read`"
          :as-gm="true"
          @sent="loadSelected"
        />

        <RulesView
          v-else-if="tab === 'tables'"
          :token="token"
          :jump-to="rulesJump"
          mode="tables"
          :gm-prefix="'/gm'"
          @follow-key="followKey"
        />
        <RulesView
          v-else-if="tab === 'rules'"
          :token="token"
          mode="rules"
          :gm-prefix="'/gm'"
          @follow-key="followKey"
        />
      </template>
    </div>
  </div>
</template>

<style scoped>
.gm-layout {
  display: flex;
  height: calc(100vh - var(--header-h, 48px));
  overflow: hidden;
}

.gm-sidebar {
  width: 220px;
  min-width: 160px;
  flex-shrink: 0;
  border-right: 1px solid var(--border);
  overflow-y: auto;
  padding: 0.5rem 0;
  background: var(--bg-sidebar, var(--bg));
}

.gm-main {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
}

.sidebar-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.25rem 0.75rem;
}

.sidebar-title {
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--muted);
}

.icon-btn {
  background: none;
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 0 6px;
  cursor: pointer;
  color: var(--text);
  line-height: 1.4;
  font-size: 1rem;
}

.icon-btn:hover { background: var(--hover-bg, #eee); }

.inline-form {
  display: flex;
  gap: 4px;
  padding: 4px 8px;
  flex-wrap: wrap;
}

.inline-input {
  flex: 1;
  min-width: 0;
  font-size: 0.85rem;
  padding: 2px 6px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--input-bg, #fff);
  color: var(--text);
}

.btn-sm {
  font-size: 0.8rem;
  padding: 2px 8px;
  border: 1px solid var(--border);
  border-radius: 4px;
  cursor: pointer;
  background: var(--btn-bg, #f0f0f0);
  color: var(--text);
}
.btn-sm.muted { color: var(--muted); }

.char-list { list-style: none; margin: 0; padding: 0; }

.char-item {
  display: flex;
  flex-direction: column;
  padding: 6px 12px;
  cursor: pointer;
  border-left: 3px solid transparent;
  position: relative;
}

.char-item:hover { background: var(--hover-bg, #f5f5f5); }
.char-item.active {
  background: var(--active-bg, #e8f0fe);
  border-left-color: var(--accent, #2d7a68);
}

.char-name { font-size: 0.9rem; font-weight: 500; }
.char-meta { font-size: 0.75rem; color: var(--muted); }

.badge {
  position: absolute;
  right: 10px;
  top: 50%;
  transform: translateY(-50%);
  background: var(--accent, #2d7a68);
  color: #fff;
  border-radius: 10px;
  font-size: 0.7rem;
  padding: 1px 6px;
  font-weight: 700;
}

.gm-char-header {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-header, var(--bg));
  font-size: 0.9rem;
}

.hp-wp { margin-left: auto; color: var(--muted); font-size: 0.85rem; }

.conditions { color: var(--danger, #c0392b); font-size: 0.8rem; }

.player-link {
  font-size: 0.8rem;
  color: var(--accent, #2d7a68);
  text-decoration: none;
}
.player-link:hover { text-decoration: underline; }

.gm-empty {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 2rem;
}

@media (max-width: 600px) {
  .gm-layout { flex-direction: column; height: auto; }
  .gm-sidebar { width: 100%; border-right: none; border-bottom: 1px solid var(--border); max-height: 200px; }
}
</style>
