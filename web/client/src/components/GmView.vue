<script setup>
// GM interface: sidebar nav on the left, content area on the right.
// Characters/parties in sidebar; reference content (tables, rules) also in sidebar — no longer in per-character tabs.
import { computed, onBeforeUnmount, onMounted, ref, watchEffect } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import ChatView from './ChatView.vue';
import CreatureView from './CreatureView.vue';
import RulesView from './RulesView.vue';
import SettingsView from './SettingsView.vue';
import SheetView from './SheetView.vue';

const POLL_MS = 3000;

const props = defineProps({
  token: { type: String, required: true },
});

// ---- state ----
const characters = ref([]);
const parties = ref([]);
const threads = ref([]);
const contentTypes = ref([]);   // [{id, label, count}] from /gm/content
const rulesChapters = ref([]);  // [{key, title, introOnly?}] from /gm/content

const selectedId = ref(null);   // selected character id, or null
const sheet = ref(null);
const chat = ref({ messages: [], gmRead: '', playerRead: '' });
// refItem: which reference panel is open; null = character/settings view
const refItem = ref(null);      // {mode: 'tables'|'rules', type: string, key: string|null, label: string}

const offline = ref(false);
const problem = ref('');
const tab = ref('sheet');       // 'sheet' | 'chat'  (tables/rules moved to sidebar)
const newCharName = ref('');
const newPartyName = ref('');
const showNewChar = ref(false);
const showNewParty = ref(false);
let timer = null;

const selected = computed(() => characters.value.find((c) => c.id === selectedId.value) ?? null);

const gmTabs = computed(() => [
  { id: 'sheet', label: 'Sheet' },
  { id: 'chat', label: threads.value.find((t) => t.characterId === selectedId.value)?.unread > 0 && tab.value !== 'chat'
      ? `Chat (${threads.value.find((t) => t.characterId === selectedId.value).unread})` : 'Chat' },
]);

// Merged, alphabetically sorted reference list
const refItems = computed(() => {
  const items = [
    ...contentTypes.value.map((t) => ({ mode: 'tables', type: t.id, label: t.label, count: t.count })),
    ...rulesChapters.value.map((r) => ({ mode: 'rules', type: r.key, label: r.title, count: null })),
  ];
  return items.sort((a, b) => a.label.localeCompare(b.label));
});

// What the main area is showing
const mainMode = computed(() => {
  if (refItem.value) return 'reference';
  if (selectedId.value) return 'character';
  return 'settings';
});

// ---- data loading ----
async function loadCharacterList() {
  const data = await apiGet('/gm/characters', props.token);
  characters.value = (data.characters ?? []).sort((a, b) => a.name.localeCompare(b.name));
}

async function loadContentSummary() {
  const data = await apiGet('/gm/content', props.token);
  contentTypes.value = data.types ?? [];
  rulesChapters.value = data.rules ?? [];
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

async function loadParties() {
  const data = await apiGet('/gm/parties', props.token);
  parties.value = data.parties ?? [];
}

async function refresh() {
  try {
    await Promise.all([loadCharacterList(), loadThreadSummaries(), loadParties()]);
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

// ---- actions ----
async function createCharacter() {
  if (!newCharName.value.trim()) return;
  try {
    await apiSend('POST', '/gm/characters', props.token, { name: newCharName.value.trim() });
    newCharName.value = '';
    showNewChar.value = false;
    await refresh();
  } catch { /* inline error later */ }
}

async function createParty() {
  if (!newPartyName.value.trim()) return;
  try {
    await apiSend('POST', '/gm/parties', props.token, { name: newPartyName.value.trim() });
    newPartyName.value = '';
    showNewParty.value = false;
    await loadParties();
  } catch { /* inline later */ }
}

// ---- navigation ----
async function selectCharacter(id) {
  refItem.value = null;
  selectedId.value = id;
  tab.value = 'sheet';
  sheet.value = null;
  chat.value = { messages: [], gmRead: '', playerRead: '' };
  await loadSelected();
}

function goSettings() {
  selectedId.value = null;
  refItem.value = null;
}

function selectRef(mode, typeKey, label) {
  refItem.value = { mode, type: typeKey, label };
}

function pick(id) { tab.value = id; }

// If the sheet links to a rule, open it in reference mode
function goToRules(target) {
  const key = target?.key ?? '';
  // Try to find it as a rules chapter first, then as a content type
  const chapter = rulesChapters.value.find((r) => r.key === key);
  if (chapter) {
    selectRef('rules', key, chapter.title);
  } else {
    const ct = contentTypes.value.find((t) => t.id === key);
    selectRef('tables', key, ct?.label ?? key);
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
  await Promise.all([refresh(), loadContentSummary()]);
  schedule();
});
onBeforeUnmount(() => clearTimeout(timer));

watchEffect(() => {
  if (refItem.value) document.title = `GM · ${refItem.value.label} · Skaldbok`;
  else if (selected.value) document.title = `GM · ${selected.value.name} · Skaldbok`;
  else document.title = 'GM · Skaldbok';
});
</script>

<template>
  <div class="gm-layout">
    <!-- ========== left sidebar ========== -->
    <aside class="gm-sidebar">

      <!-- General Settings -->
      <button class="nav-item" :class="{ active: mainMode === 'settings' }" @click="goSettings">
        ⚙ General Settings
      </button>

      <!-- Characters -->
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
          :class="{ active: c.id === selectedId && mainMode === 'character' }"
          @click="selectCharacter(c.id)"
        >
          <span class="char-name">{{ c.name || c.id }}</span>
          <span class="char-meta">{{ c.kin }} {{ c.profession }}</span>
          <span v-if="threads.find((t) => t.characterId === c.id)?.unread" class="badge">
            {{ threads.find((t) => t.characterId === c.id).unread }}
          </span>
        </li>
      </ul>

      <!-- Parties -->
      <div class="sidebar-head" style="margin-top: 0.75rem;">
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

      <!-- Reference: content types + rules chapters, sorted alphabetically -->
      <div v-if="refItems.length" class="sidebar-head" style="margin-top: 0.75rem;">
        <span class="sidebar-title">Reference</span>
      </div>
      <ul v-if="refItems.length" class="char-list">
        <li
          v-for="r in refItems"
          :key="r.mode + r.type"
          class="char-item ref-item"
          :class="{ active: refItem?.type === r.type && refItem?.mode === r.mode }"
          @click="selectRef(r.mode, r.type, r.label)"
        >
          <span class="char-name">{{ r.label }}</span>
          <span v-if="r.count !== null" class="ref-count">{{ r.count }}</span>
        </li>
      </ul>

    </aside>

    <!-- ========== main area ========== -->
    <div class="gm-main">

      <!-- Error banner -->
      <p v-if="problem" class="error" style="padding: 1rem;">{{ problem }}</p>
      <p v-else-if="offline && mainMode !== 'reference'" class="error" style="padding: 1rem;">Cannot reach the server. Trying again…</p>

      <!-- Settings -->
      <SettingsView v-if="mainMode === 'settings' && !problem" :token="token" />

      <!-- Reference panel -->
      <template v-else-if="mainMode === 'reference'">
        <div class="ref-header">
          <strong>{{ refItem.label }}</strong>
        </div>
        <CreatureView
          v-if="refItem.type === 'creatures'"
          :token="token"
        />
        <RulesView
          v-else
          :key="refItem.type + refItem.mode"
          :token="token"
          :mode="refItem.mode"
          :jump-to="{ type: refItem.type, key: null }"
          :gm-prefix="'/gm'"
        />
      </template>

      <!-- Character panel -->
      <template v-else-if="mainMode === 'character'">
        <div class="gm-char-header">
          <strong>{{ selected.name }}</strong>
          <span class="char-meta">{{ selected.kin }} {{ selected.profession }} · {{ selected.age }}</span>
          <span v-if="selected.conditions.length" class="conditions">{{ selected.conditions.join(', ') }}</span>
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
  width: 180px;
  min-width: 140px;
  flex-shrink: 0;
  border-right: 1px solid var(--border);
  overflow-y: auto;
  padding: 0.5rem 0;
  background: var(--bg-sidebar, var(--bg));
}

.gm-main {
  flex: 1;
  overflow-y: auto;
  overflow-x: hidden;
  display: flex;
  flex-direction: column;
  scrollbar-width: thin;
  scrollbar-color: rgba(0,0,0,0.18) transparent;
}
.gm-main::-webkit-scrollbar { width: 6px; }
.gm-main::-webkit-scrollbar-track { background: transparent; }
.gm-main::-webkit-scrollbar-thumb {
  background: rgba(0,0,0,0.18);
  border-radius: 99px;
}
.gm-main::-webkit-scrollbar-thumb:hover { background: rgba(0,0,0,0.32); }

.gm-sidebar {
  scrollbar-width: thin;
  scrollbar-color: rgba(0,0,0,0.12) transparent;
}
.gm-sidebar::-webkit-scrollbar { width: 4px; }
.gm-sidebar::-webkit-scrollbar-track { background: transparent; }
.gm-sidebar::-webkit-scrollbar-thumb {
  background: rgba(0,0,0,0.12);
  border-radius: 99px;
}
.gm-sidebar::-webkit-scrollbar-thumb:hover { background: rgba(0,0,0,0.25); }

/* ---- sidebar nav ---- */
.nav-item {
  display: block;
  width: 100%;
  text-align: left;
  padding: 8px 12px;
  font-size: 0.85rem;
  font-weight: 500;
  border: none;
  border-left: 3px solid transparent;
  background: none;
  cursor: pointer;
  color: var(--text);
  margin-bottom: 2px;
}
.nav-item:hover { background: var(--hover-bg, #f5f5f5); }
.nav-item.active {
  background: var(--active-bg, #e8f0fe);
  border-left-color: var(--accent, #2d7a68);
}

.sidebar-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.25rem 0.75rem;
}

.sidebar-title {
  font-size: 0.7rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.07em;
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
  padding: 5px 12px;
  cursor: pointer;
  border-left: 3px solid transparent;
  position: relative;
}
.char-item:hover { background: var(--hover-bg, #f5f5f5); }
.char-item.active {
  background: var(--active-bg, #e8f0fe);
  border-left-color: var(--accent, #2d7a68);
}

.char-name { font-size: 0.88rem; font-weight: 500; }
.char-meta { font-size: 0.73rem; color: var(--muted); }

.ref-item { flex-direction: row; align-items: center; }
.ref-item .char-name { flex: 1; }
.ref-count {
  font-size: 0.75rem;
  color: var(--muted);
  margin-left: 4px;
}

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

/* ---- main area ---- */
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

.ref-header {
  padding: 8px 16px;
  border-bottom: 1px solid var(--border);
  font-size: 0.95rem;
}

.hp-wp { margin-left: auto; color: var(--muted); font-size: 0.85rem; }
.conditions { color: var(--danger, #c0392b); font-size: 0.8rem; }

.player-link {
  font-size: 0.8rem;
  color: var(--accent, #2d7a68);
  text-decoration: none;
}
.player-link:hover { text-decoration: underline; }

.muted { color: var(--muted); }
.error { color: var(--danger, #c0392b); }

@media (max-width: 600px) {
  .gm-layout { flex-direction: column; height: auto; }
  .gm-sidebar { width: 100%; border-right: none; border-bottom: 1px solid var(--border); max-height: 260px; }
}
</style>
