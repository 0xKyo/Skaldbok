<script setup>
// GM interface: tabs along the top, like the player's page. Character (a dropdown of every character, with its sheet), Chat and Rules
// (the Reference, the same the players have).
import { computed, onBeforeUnmount, onMounted, ref, watchEffect } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import ChatView from './ChatView.vue';
import CharacterCreator from './CharacterCreator.vue';
import ReferenceView from './ReferenceView.vue';
import { useReference } from '../reference.js';
import SheetView from './SheetView.vue';

const POLL_MS = 3000;

const props = defineProps({
  token: { type: String, required: true },
});

// ---- state ----
const characters = ref([]);
const threads = ref([]);

const tab = ref('character');       // 'character' | 'chat' | 'rules'
const selectedId = ref(null);       // the character the Character and Chat tabs show
const sheet = ref(null);
const chat = ref({ messages: [], gmRead: '', playerRead: '' });
const reference = useReference(props.token, '/gm');

const offline = ref(false);
const problem = ref('');
const creator = ref(null);          // null, or the creator is open: 'wizard' from the first step, 'random' already filled in
const confirmingCharDelete = ref(false);
let timer = null;

const selected = computed(() => characters.value.find((c) => c.id === selectedId.value) ?? null);
const unreadOf = (id) => threads.value.find((t) => t.characterId === id)?.unread ?? 0;
const totalUnread = computed(() => threads.value.reduce((n, t) => n + (t.unread ?? 0), 0));

const tabs = computed(() => [
  { id: 'character', label: 'Character' },
  { id: 'chat', label: totalUnread.value > 0 && tab.value !== 'chat' ? `Chat (${totalUnread.value})` : 'Chat' },
  { id: 'rules', label: 'Rules' },
]);

// ---- data loading ----
async function loadCharacterList() {
  const data = await apiGet('/gm/characters', props.token);
  characters.value = (data.characters ?? []).sort((a, b) => a.name.localeCompare(b.name));
}

async function loadSelected() {
  if (!selectedId.value) return;
  const id = selectedId.value;
  const [sheetData, chatData] = await Promise.all([
    apiGet(`/gm/characters/${id}`, props.token),
    apiGet(`/gm/chat/${id}`, props.token).catch(() => null),
  ]);
  if (id !== selectedId.value) return;                 // another character was picked meanwhile
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
    // the dropdown always has a character picked while there is one
    if (!characters.value.some((c) => c.id === selectedId.value)) {
      selectedId.value = null;
      sheet.value = null;
      if (characters.value.length && !creator.value) await selectCharacter(characters.value[0].id);
    }
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

// ---- characters ----
async function selectCharacter(id) {
  creator.value = null;
  confirmingCharDelete.value = false;
  selectedId.value = id || null;
  sheet.value = null;
  chat.value = { messages: [], gmRead: '', playerRead: '' };
  if (id) await loadSelected();
}

function openCreator(mode) {
  tab.value = 'character';
  creator.value = mode;
}

async function characterCreated(id) {
  creator.value = null;
  await refresh();
  tab.value = 'character';
  await selectCharacter(id);
}

// Deleting a character removes its file; it asks first.
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

// If the sheet links to a rule, open it in the Rules tab
function goToRules(target) {
  tab.value = 'rules';
  reference.goTo(target);
}

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
  if (tab.value === 'rules' && reference.current) document.title = `GM · ${reference.current.label} · Skaldbok`;
  else if (tab.value === 'character' && selected.value) document.title = `GM · ${selected.value.name} · Skaldbok`;
  else document.title = 'GM · Skaldbok';
});
</script>

<template>
  <div class="gm-view">
    <nav class="tabs" role="tablist">
      <button v-for="t in tabs" :key="t.id" role="tab" :aria-selected="tab === t.id" data-test="gm-tab" @click="tab = t.id">{{ t.label }}</button>
    </nav>

    <p v-if="problem" class="error" style="padding: 1rem;">{{ problem }}</p>
    <p v-else-if="offline" class="error" style="padding: 0 0 1rem;">Cannot reach the server. Trying again…</p>

    <template v-if="!problem">
      <!-- the character picked in the dropdown: Character shows its sheet, Chat its conversation -->
      <div v-if="(tab === 'character' && !creator) || tab === 'chat'" class="pick-bar">
        <select :value="selectedId ?? ''" aria-label="Character" data-test="character-select" :disabled="!characters.length" @change="selectCharacter($event.target.value)">
          <option v-if="!characters.length" value="">No characters yet</option>
          <option v-for="c in characters" :key="c.id" :value="c.id">{{ c.name || c.id }}{{ unreadOf(c.id) ? ` (${unreadOf(c.id)})` : '' }} · {{ c.kin }} {{ c.profession }}</option>
        </select>
        <template v-if="tab === 'character'">
          <button type="button" class="pick-btn" data-test="new-character" @click="openCreator('wizard')">+ New character</button>
          <button type="button" class="pick-btn" data-test="new-random" @click="openCreator('random')">Random</button>
          <span v-if="selected" class="char-actions">
            <a v-if="selected.link" :href="selected.link" target="_blank" class="player-link">Player link ↗</a>
            <button v-if="!confirmingCharDelete" type="button" class="del-btn" data-test="delete-character" @click="confirmingCharDelete = true">Delete character</button>
            <span v-else class="confirm" data-test="char-delete-confirm">Delete «{{ selected.name }}»? This cannot be undone.
              <button type="button" class="del-btn sure" data-test="char-delete-yes" @click="deleteCharacter">Delete</button>
              <button type="button" class="del-btn" data-test="char-delete-no" @click="confirmingCharDelete = false">Cancel</button>
            </span>
          </span>
        </template>
      </div>

      <!-- Character -->
      <template v-if="tab === 'character'">
        <CharacterCreator v-if="creator" :key="creator" :token="token" :random="creator === 'random'" @created="characterCreated" @cancel="creator = null" />
        <template v-else-if="selected">
          <SheetView
            v-if="sheet"
            :me="sheet"
            :token="token"
            :patch-path="`/gm/characters/${selectedId}`"
            @updated="(fresh) => { sheet = fresh; loadCharacterList(); }"
            @goto-rules="goToRules"
          />
          <p v-else class="muted" style="padding: 1rem;">Loading…</p>
        </template>
        <p v-else class="muted" style="padding: 1rem;">There are no characters yet. Use “+ New character” or “Random”.</p>
      </template>

      <!-- Chat: for now the player's own view, of the character picked above -->
      <template v-else-if="tab === 'chat'">
        <ChatView
          v-if="selectedId"
          :key="selectedId"
          :thread="chat"
          :token="token"
          :send-path="`/gm/chat/${selectedId}`"
          :read-path="`/gm/chat/${selectedId}/read`"
          :as-gm="true"
          @sent="loadSelected"
        />
        <p v-else class="muted" style="padding: 1rem;">There are no characters to talk to yet.</p>
      </template>

      <!-- Rules: the Reference, exactly the players' -->
      <ReferenceView v-else-if="tab === 'rules'" :state="reference" :token="token" gm-prefix="/gm" />
    </template>
  </div>
</template>

<style scoped>
.pick-bar { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; margin-bottom: 12px; }
.pick-bar select { flex: 1 1 260px; max-width: 460px; font: inherit; padding: 6px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; color: var(--ink); }
.pick-btn { padding: 5px 14px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.85rem; font-weight: 600; cursor: pointer; }
.pick-btn:hover:not(:disabled) { background: var(--green-dark); color: #f3ead2; }
.pick-btn:disabled { opacity: 0.45; cursor: default; }

.char-actions { margin-left: auto; display: inline-flex; align-items: center; gap: 12px; flex-wrap: wrap; justify-content: flex-end; }
.player-link { font-size: 0.85rem; color: var(--green-dark); }
.player-link:hover { text-decoration: underline; }
.del-btn { padding: 3px 12px; border: 1px solid var(--line); border-radius: 20px; background: none; color: var(--muted); font: inherit; font-size: 0.8rem; cursor: pointer; }
.del-btn:hover { color: var(--danger, #c0392b); border-color: var(--danger, #c0392b); }
.del-btn.sure { background: var(--danger, #c0392b); border-color: var(--danger, #c0392b); color: #fff; }
.confirm { display: inline-flex; align-items: center; gap: 8px; flex-wrap: wrap; font-size: 0.85rem; color: var(--danger, #c0392b); }

.muted { color: var(--muted); }
</style>
