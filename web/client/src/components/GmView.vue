<script setup>
// GM interface: sidebar nav on the left, content area on the right.
// Characters/parties in sidebar; reference content (tables, rules) also in sidebar — no longer in per-character tabs.
import { computed, onBeforeUnmount, onMounted, ref, watchEffect } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import ChatView from './ChatView.vue';
import CharacterCreator from './CharacterCreator.vue';
import GmPartyPanel from './GmPartyPanel.vue';
import ReferenceIndex from './ReferenceIndex.vue';
import ReferencePanel from './ReferencePanel.vue';
import { useReference } from '../reference.js';
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

const selectedId = ref(null);   // selected character id, or null
const selectedPartyId = ref(null); // selected party id, or null
const partiesOpen = ref(true);  // the Parties section of the sidebar is a dropdown
const expanded = ref(new Set()); // parties whose characters are shown under them
const sheet = ref(null);
const chat = ref({ messages: [], gmRead: '', playerRead: '' });
// reference.current: which reference panel is open; null = character/settings view
const reference = useReference(props.token, '/gm');

const offline = ref(false);
const problem = ref('');
const tab = ref('sheet');       // 'sheet' | 'chat'  (tables/rules moved to sidebar)
const newPartyName = ref('');
const creator = ref(null);      // null, or the creator is open: 'wizard' from the first step, 'random' already filled in
const showNewParty = ref(false);
let timer = null;

const selected = computed(() => characters.value.find((c) => c.id === selectedId.value) ?? null);
const selectedParty = computed(() => parties.value.find((p) => p.id === selectedPartyId.value) ?? null);
// a character can be in several parties, or in none: those go in the Solo category of Characters
const inAParty = computed(() => new Set(parties.value.flatMap((p) => p.members)));
const soloCharacters = computed(() => characters.value.filter((c) => !inAParty.value.has(c.id)));
const membersOf = (p) => p.members.map((id) => characters.value.find((c) => c.id === id)).filter(Boolean);
const unreadOf = (id) => threads.value.find((t) => t.characterId === id)?.unread ?? 0;

const gmTabs = computed(() => [
  { id: 'sheet', label: 'Sheet' },
  { id: 'chat', label: threads.value.find((t) => t.characterId === selectedId.value)?.unread > 0 && tab.value !== 'chat'
      ? `Chat (${threads.value.find((t) => t.characterId === selectedId.value).unread})` : 'Chat' },
]);

// What the main area is showing
const mainMode = computed(() => {
  if (reference.current) return 'reference';
  if (creator.value) return 'creator';
  if (selectedParty.value) return 'party';
  if (selectedId.value) return 'character';
  return 'settings';
});

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
function openCreator(mode) {
  reference.current = null;
  selectedPartyId.value = null;
  selectedId.value = null;
  creator.value = mode;
}

async function characterCreated(id) {
  creator.value = null;
  await refresh();
  await selectCharacter(id);
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

// Deleting a character removes its file (and it leaves every party); it asks first.
const confirmingCharDelete = ref(false);
async function deleteCharacter() {
  const id = selectedId.value;
  if (!id) return;
  try {
    await apiSend('DELETE', `/gm/characters/${id}`, props.token);
  } catch { confirmingCharDelete.value = false; return; }
  confirmingCharDelete.value = false;
  selectedId.value = null;
  sheet.value = null;
  await refresh();
}

// ---- navigation ----
async function selectCharacter(id) {
  creator.value = null;
  confirmingCharDelete.value = false;
  reference.current = null;
  selectedPartyId.value = null;
  selectedId.value = id;
  tab.value = 'sheet';
  sheet.value = null;
  chat.value = { messages: [], gmRead: '', playerRead: '' };
  await loadSelected();
}

// A party opens its own page (name, members, a message to all of them) and shows its characters under it in the sidebar.
function selectParty(id) {
  creator.value = null;
  reference.current = null;
  selectedId.value = null;
  selectedPartyId.value = id;
  expanded.value.add(id);
}

async function afterPartyDeleted(id) {
  expanded.value.delete(id);
  selectedPartyId.value = null;
  await loadParties();
}

function toggleParty(id) {
  if (expanded.value.has(id)) expanded.value.delete(id);
  else expanded.value.add(id);
}

function goSettings() {
  creator.value = null;
  selectedId.value = null;
  selectedPartyId.value = null;
  reference.current = null;
}

function pick(id) { tab.value = id; }

// If the sheet links to a rule, open it in reference mode
function goToRules(target) { reference.goTo(target); }

function schedule() {
  clearTimeout(timer);
  timer = setTimeout(async () => {
    if (!document.hidden) await refresh();
    schedule();
  }, POLL_MS);
}

onMounted(async () => {
  await Promise.all([refresh(), reference.load().catch(() => {})]);
  schedule();
});
onBeforeUnmount(() => clearTimeout(timer));

watchEffect(() => {
  if (reference.current) document.title = `GM · ${reference.current.label} · Skaldbok`;
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
        <span class="head-actions">
          <button class="icon-btn" title="Random character" data-test="new-random" @click="openCreator('random')">🎲</button>
          <button class="icon-btn" title="New character" data-test="new-character" @click="openCreator('wizard')">+</button>
        </span>
      </div>
      <div class="sidebar-sub" data-test="solo-title">Solo <span class="muted">{{ soloCharacters.length }}</span></div>
      <ul class="char-list" data-test="solo-list">
        <li
          v-for="c in soloCharacters"
          :key="c.id"
          class="char-item"
          :class="{ active: c.id === selectedId && mainMode === 'character' }"
          @click="selectCharacter(c.id)"
        >
          <span class="char-name">{{ c.name || c.id }}</span>
          <span class="char-meta">{{ c.kin }} {{ c.profession }}</span>
          <span v-if="unreadOf(c.id)" class="badge">{{ unreadOf(c.id) }}</span>
        </li>
      </ul>

      <!-- Parties: a dropdown of parties, each one a dropdown of its characters -->
      <div class="sidebar-head" style="margin-top: 0.75rem;">
        <button class="section-toggle" :aria-expanded="partiesOpen" data-test="parties-toggle" @click="partiesOpen = !partiesOpen">
          <span class="chev">{{ partiesOpen ? '▾' : '▸' }}</span><span class="sidebar-title">Parties</span>
        </button>
        <button class="icon-btn" title="New party" @click="showNewParty = !showNewParty; partiesOpen = true">+</button>
      </div>
      <form v-if="showNewParty" class="inline-form" @submit.prevent="createParty">
        <input v-model="newPartyName" placeholder="Party name…" class="inline-input" />
        <button type="submit" class="btn-sm">Create</button>
        <button type="button" class="btn-sm muted" @click="showNewParty = false">Cancel</button>
      </form>
      <ul v-if="partiesOpen" class="char-list" data-test="party-list">
        <li v-for="p in parties" :key="p.id" class="party-item" data-test="party-item">
          <div class="char-item party-row" :class="{ active: p.id === selectedPartyId && mainMode === 'party' }">
            <button class="chev-btn" :aria-label="expanded.has(p.id) ? 'Hide characters' : 'Show characters'" :aria-expanded="expanded.has(p.id)" data-test="party-chevron" @click.stop="toggleParty(p.id)">{{ expanded.has(p.id) ? '▾' : '▸' }}</button>
            <button class="party-open" data-test="party-open" @click="selectParty(p.id)">
              <span class="char-name">{{ p.name }}</span>
              <span class="char-meta">{{ membersOf(p).length }} member{{ membersOf(p).length !== 1 ? 's' : '' }}</span>
            </button>
          </div>
          <ul v-if="expanded.has(p.id)" class="party-members">
            <li
              v-for="c in membersOf(p)"
              :key="c.id"
              class="char-item"
              :class="{ active: c.id === selectedId && mainMode === 'character' }"
              data-test="party-character"
              @click="selectCharacter(c.id)"
            >
              <span class="char-name">{{ c.name || c.id }}</span>
              <span class="char-meta">{{ c.kin }} {{ c.profession }}</span>
              <span v-if="unreadOf(c.id)" class="badge">{{ unreadOf(c.id) }}</span>
            </li>
            <li v-if="!membersOf(p).length" class="muted small party-empty">No characters yet.</li>
          </ul>
        </li>
      </ul>

      <!-- Reference: content types + rules chapters, sorted alphabetically -->
      <div v-if="reference.items.length" class="sidebar-head" style="margin-top: 0.75rem;">
        <span class="sidebar-title">Reference</span>
      </div>
      <ReferenceIndex v-if="reference.items.length" :state="reference" />

    </aside>

    <!-- ========== main area ========== -->
    <div class="gm-main">

      <!-- Error banner -->
      <p v-if="problem" class="error" style="padding: 1rem;">{{ problem }}</p>
      <p v-else-if="offline && mainMode !== 'reference'" class="error" style="padding: 1rem;">Cannot reach the server. Trying again…</p>

      <!-- Settings -->
      <SettingsView v-if="mainMode === 'settings' && !problem" :token="token" />

      <!-- Character creator -->
      <CharacterCreator v-if="mainMode === 'creator'" :key="creator" :token="token" :random="creator === 'random'" @created="characterCreated" @cancel="creator = null" />

      <!-- Reference panel -->
      <ReferencePanel v-else-if="mainMode === 'reference'" :state="reference" :token="token" gm-prefix="/gm" />

      <!-- Party panel -->
      <GmPartyPanel
        v-else-if="mainMode === 'party'"
        :key="selectedParty.id"
        :party="selectedParty"
        :characters="characters"
        :token="token"
        @changed="loadParties"
        @deleted="afterPartyDeleted"
        @open-character="selectCharacter"
      />

      <!-- Character panel -->
      <template v-else-if="mainMode === 'character'">
        <div class="gm-char-header">
          <strong>{{ selected.name }}</strong>
          <span class="char-meta">{{ selected.kin }} {{ selected.profession }} · {{ selected.age }}</span>
          <span v-if="selected.conditions.length" class="conditions">{{ selected.conditions.join(', ') }}</span>
          <span class="hp-wp">HP {{ selected.hp.current }}/{{ selected.hp.max }} · WP {{ selected.wp.current }}/{{ selected.wp.max }}</span>
          <a v-if="selected.link" :href="selected.link" target="_blank" class="player-link">Player link ↗</a>
          <button v-if="!confirmingCharDelete" type="button" class="del-btn" data-test="delete-character" @click="confirmingCharDelete = true">Delete character</button>
          <span v-else class="confirm" data-test="char-delete-confirm">Delete «{{ selected.name }}»? This cannot be undone.
            <button type="button" class="del-btn sure" data-test="char-delete-yes" @click="deleteCharacter">Delete</button>
            <button type="button" class="del-btn" data-test="char-delete-no" @click="confirmingCharDelete = false">Cancel</button>
          </span>
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

.head-actions { display: inline-flex; gap: 2px; }
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

/* ---- Solo category and the Parties dropdown ---- */
.sidebar-sub {
  padding: 4px 12px 2px;
  font-size: 0.72rem;
  font-weight: 700;
  letter-spacing: 0.05em;
  text-transform: uppercase;
  color: var(--muted);
}
.section-toggle { display: flex; align-items: center; gap: 4px; padding: 0; border: 0; background: none; font: inherit; cursor: pointer; color: inherit; }
.chev { width: 1em; color: var(--muted); }
.party-item { list-style: none; }
.party-row { flex-direction: row; align-items: center; gap: 2px; padding: 0 12px 0 4px; }
.chev-btn { width: 22px; flex: 0 0 22px; padding: 6px 0; border: 0; background: none; color: var(--muted); cursor: pointer; font-size: 0.8rem; }
.party-open { flex: 1; min-width: 0; display: flex; flex-direction: column; padding: 5px 0; border: 0; background: none; font: inherit; text-align: left; color: inherit; cursor: pointer; }
.party-members { list-style: none; margin: 0; padding: 0 0 0 16px; }
.party-empty { padding: 3px 12px 3px 26px; }

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

.hp-wp { margin-left: auto; color: var(--muted); font-size: 0.85rem; }
.conditions { color: var(--danger, #c0392b); font-size: 0.8rem; }

.player-link {
  font-size: 0.8rem;
  color: var(--accent, #2d7a68);
  text-decoration: none;
}
.player-link:hover { text-decoration: underline; }
.del-btn { margin-left: auto; padding: 3px 12px; border: 1px solid var(--line); border-radius: 20px; background: none; color: var(--muted); font: inherit; font-size: 0.8rem; cursor: pointer; }
.del-btn:hover { color: var(--danger, #c0392b); border-color: var(--danger, #c0392b); }
.del-btn.sure { margin-left: 0; background: var(--danger, #c0392b); border-color: var(--danger, #c0392b); color: #fff; }
.confirm { margin-left: auto; display: inline-flex; align-items: center; gap: 8px; flex-wrap: wrap; font-size: 0.85rem; color: var(--danger, #c0392b); }
.confirm .del-btn { margin-left: 0; }

.muted { color: var(--muted); }
.error { color: var(--danger, #c0392b); }

@media (max-width: 600px) {
  .gm-layout { flex-direction: column; height: auto; }
  .gm-sidebar { width: 100%; border-right: none; border-bottom: 1px solid var(--border); max-height: 260px; }
}
</style>
