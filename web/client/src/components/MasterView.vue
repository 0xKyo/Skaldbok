<script setup>
// The Master tab has two parts. The Screen is the board where the GM pins whatever is needed during the game (a rule, a table, an NPC, a picture)
// and writes notes. Campaigns are what is played: each has a name, the characters that play it, and its sessions — a board for each game: what
// happened, what was found. One session is active at a time (what is sent from the Screen goes to it). End Session closes it (it keeps its date and
// can be looked at, not changed); Open Session makes a closed one the active one again (and closes the other). The campaign, who plays it and its
// session are one line of dropdowns and buttons.
import { computed, inject, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { locale, t } from '../i18n.js';
import MasterBoard from './MasterBoard.vue';

defineProps({
  token: { type: String, required: true },
});

const sessions = inject('sessions');
const campaigns = inject('campaigns');
const characters = inject('characters', ref([]));      // the characters that exist: [{id, name, kin, profession}]

const part = ref(window.location.hash.startsWith('#master/') ? 'campaigns' : 'screen');       // 'screen' | 'campaigns' (kept in the address: #master/campaigns)
const campaignId = ref('');                // the campaign being looked at
const sessionId = ref('');                 // its session being looked at
const confirming = ref('');                // '', 'end', 'new', 'delete' or 'delete-campaign': what is being asked
const renaming = ref('');                  // '', 'campaign' or 'session': the name being written in place of the dropdown
const campaignDraft = ref('');
const nameDraft = ref('');
const nameInput = ref(null);
const picker = ref(null);                  // the dropdown of characters
const pickerOpen = ref(false);

function closePicker(e) {
  if (pickerOpen.value && picker.value && !picker.value.contains(e.target)) pickerOpen.value = false;
}
onMounted(() => {
  sessions.refresh();
  campaigns.refresh();
  document.addEventListener('click', closePicker);
});
onBeforeUnmount(() => document.removeEventListener('click', closePicker));
watch(part, (p) => window.history.replaceState(window.history.state, '', `${window.location.pathname}${window.location.search}#master${p === 'campaigns' ? '/campaigns' : ''}`));

const campaign = computed(() => campaigns.find(campaignId.value));
const SHIFTS = [1, 2, 3, 4];                // a day of play: tapping the last filled circle empties it, any other fills up to itself
const setShifts = (n) => campaigns.setShifts(campaign.value.id, (campaign.value.shifts ?? 0) === n ? n - 1 : n);
const campaignSessions = computed(() => sessions.list.value.filter((s) => s.campaign === campaignId.value));
const current = computed(() => campaignSessions.value.find((s) => s.id === sessionId.value) ?? null);
const isActive = computed(() => !!current.value && current.value.id === sessions.active.value);
const closed = computed(() => !!current.value?.ended);
const members = computed(() => (campaign.value?.characters ?? []).map((id) => characters.value.find((c) => c.id === id)).filter(Boolean));
const peopleLabel = computed(() => (members.value.length ? members.value.map((c) => c.name || c.id).join(', ') : t('Nobody yet')));

// the campaign looked at is the one of the active session, or else the newest, until another is chosen; its session is the active one, or the newest
watch([() => campaigns.list.value, () => sessions.active.value], () => {
  if (!campaigns.find(campaignId.value)) campaignId.value = sessions.activeSession()?.campaign || campaigns.list.value[0]?.id || '';
}, { immediate: true });
watch([campaignId, () => sessions.list.value], () => {
  if (!campaignSessions.value.some((s) => s.id === sessionId.value)) {
    sessionId.value = campaignSessions.value.find((s) => s.id === sessions.active.value)?.id ?? campaignSessions.value[0]?.id ?? '';
  }
}, { immediate: true });
watch([campaignId, sessionId], () => { confirming.value = ''; renaming.value = ''; });

// only the day matters, as in "Oct 2, 2026"
const dayOf = (iso) => {
  const d = iso ? new Date(iso) : null;
  return d && !Number.isNaN(d.getTime()) ? d.toLocaleDateString(locale(), { month: 'short', day: 'numeric', year: 'numeric' }) : '';
};
const label = (s) => `${s.id === sessions.active.value ? '● ' : ''}${s.name} · ${dayOf(s.created)}`;

// ---- renaming: the dropdown becomes the name, until Enter or leaving it (Esc leaves it as it was) ----
async function startRename(what) {
  campaignDraft.value = campaign.value?.name ?? '';
  nameDraft.value = current.value?.name ?? '';
  renaming.value = what;
  await nextTick();
  nameInput.value?.focus();
  nameInput.value?.select();
}
async function finishRename(save = true) {
  const what = renaming.value;
  renaming.value = '';
  if (!save) return;
  if (what === 'campaign') await campaigns.rename(campaignId.value, campaignDraft.value);
  else if (what === 'session') await sessions.rename(current.value.id, nameDraft.value);
}

// ---- campaigns ----
async function newCampaign() {
  const id = await campaigns.create();
  if (id) campaignId.value = id;
}

async function deleteCampaign() {
  if (confirming.value !== 'delete-campaign') {
    confirming.value = 'delete-campaign';
    return;
  }
  confirming.value = '';
  const after = await campaigns.remove(campaignId.value);
  if (after) sessions.take({ sessions: after.sessions, active: after.active });
}

function toggleCharacter(id) {
  const now = new Set(campaign.value.characters);
  if (now.has(id)) now.delete(id);
  else now.add(id);
  campaigns.setCharacters(campaignId.value, [...now]);
}

// ---- sessions ----
async function startNew() {
  if (sessions.active.value && confirming.value !== 'new') {          // there is one open: starting another closes it, so it asks first
    confirming.value = 'new';
    return;
  }
  confirming.value = '';
  const id = await sessions.create(campaignId.value);
  if (id) {
    await campaigns.refresh();
    sessionId.value = id;
  }
}

async function endCurrent() {
  if (confirming.value !== 'end') {
    confirming.value = 'end';
    return;
  }
  confirming.value = '';
  await sessions.end(current.value.id);
}

async function deleteCurrent() {
  if (confirming.value !== 'delete') {
    confirming.value = 'delete';
    return;
  }
  confirming.value = '';
  if (await sessions.remove(current.value.id)) await campaigns.refresh();
}

const openCurrent = async () => { await sessions.open(current.value.id); };
</script>

<template>
  <div class="master" data-test="master">
    <nav class="parts" :aria-label="t('Master')">
      <button type="button" class="part" :class="{ active: part === 'screen' }" data-test="part-screen" @click="part = 'screen'">{{ t('Master Screen') }}</button>
      <button type="button" class="part" :class="{ active: part === 'campaigns' }" data-test="part-campaigns" @click="part = 'campaigns'">{{ t('Campaigns') }}</button>
      <!-- the shifts of the day of the campaign being looked at: four circles that fill as they pass -->
      <span v-if="campaign" class="shifts" data-test="shifts">
        <span class="shifts-label">{{ t('Shifts') }}</span>
        <button v-for="i in SHIFTS" :key="i" type="button" class="shift" :class="{ on: i <= (campaign.shifts ?? 0) }" :aria-pressed="i <= (campaign.shifts ?? 0)"
                :aria-label="t('Shifts') + ' ' + i" data-test="shift" @click="setShifts(i)"></button>
        <button type="button" class="shifts-reset" :title="t('Shifts back to 0, and the rests of the players given back')" data-test="shifts-reset" @click="campaigns.resetShifts(campaign.id)">{{ t('Reset') }}</button>
      </span>
    </nav>

    <MasterBoard v-if="part === 'screen'" :token="token" />

    <template v-else>
      <p v-if="sessions.error.value || campaigns.error.value" class="error" data-test="sessions-error">
        {{ campaigns.error.value || sessions.error.value }} <button type="button" class="btn" @click="campaigns.refresh(); sessions.refresh()">{{ t('Try again') }}</button>
      </p>

      <!-- everything on one line: the campaign, who plays it, its session, and what can be done -->
      <div class="bar" data-test="campaign-bar">
        <template v-if="campaigns.list.value.length">
          <span class="group" data-test="campaign-group">
            <input v-if="renaming === 'campaign'" ref="nameInput" v-model="campaignDraft" class="pick name" maxlength="80" :aria-label="t('Campaign name')" data-test="campaign-name"
                   @keydown.enter.prevent="finishRename()" @keydown.esc.prevent="finishRename(false)" @blur="finishRename()" />
            <select v-else v-model="campaignId" class="pick" :aria-label="t('Campaign')" data-test="campaign-pick">
              <option v-for="c in campaigns.list.value" :key="c.id" :value="c.id">{{ c.name }}</option>
            </select>
            <button type="button" class="icon" :title="t('Rename the campaign')" :aria-label="t('Rename the campaign')" data-test="rename-campaign" @mousedown.prevent @click="startRename('campaign')">✎</button>
            <button type="button" class="icon danger" :title="t('Delete the campaign')" :aria-label="t('Delete the campaign')" data-test="delete-campaign" @click="deleteCampaign">✕</button>
          </span>

          <div ref="picker" class="dropdown" data-test="characters-dropdown">
            <button type="button" class="drop-btn" :aria-expanded="pickerOpen" data-test="characters-button" @click="pickerOpen = !pickerOpen">
              <span class="drop-label">{{ t('Characters') }}</span> <span class="drop-value" data-test="characters-summary">{{ peopleLabel }}</span> ▾
            </button>
            <div v-if="pickerOpen" class="drop-panel" role="listbox" :aria-label="t('Characters of the campaign')" data-test="characters-panel">
              <p v-if="!characters.length" class="muted small">{{ t('There are no characters yet.') }}</p>
              <label v-for="c in characters" :key="c.id" class="check-row" data-test="character-row">
                <input type="checkbox" :checked="campaign.characters.includes(c.id)" @change="toggleCharacter(c.id)" />
                <span>{{ c.name || c.id }}<span v-if="c.kin || c.profession" class="muted small"> · {{ c.kin }} {{ c.profession }}</span></span>
              </label>
            </div>
          </div>

          <span class="group" data-test="sessions-bar">
            <template v-if="campaignSessions.length">
              <input v-if="renaming === 'session'" ref="nameInput" v-model="nameDraft" class="pick name" maxlength="80" :aria-label="t('Session name')" data-test="session-name"
                     @keydown.enter.prevent="finishRename()" @keydown.esc.prevent="finishRename(false)" @blur="finishRename()" />
              <select v-else v-model="sessionId" class="pick" :aria-label="t('Session')" data-test="session-pick">
                <option v-for="s in campaignSessions" :key="s.id" :value="s.id">{{ label(s) }}</option>
              </select>
              <button type="button" class="icon" :title="t('Rename the session')" :aria-label="t('Rename the session')" data-test="rename-session" @mousedown.prevent @click="startRename('session')">✎</button>
              <button type="button" class="icon danger" :title="t('Delete the session')" :aria-label="t('Delete the session')" data-test="delete-session" @click="deleteCurrent">✕</button>
              <span class="state" data-test="session-dates">
                <span v-if="isActive" class="badge on">{{ t('Active') }}</span><span v-else class="badge">{{ t('Closed') }}</span>
              </span>
            </template>
            <span v-else class="muted nosessions" data-test="no-sessions">{{ t('No sessions yet.') }}</span>
          </span>
        </template>
        <span v-else class="muted" data-test="no-campaigns">{{ t('No campaigns yet.') }}</span>

        <span class="actions">
          <button v-if="current && isActive" type="button" class="btn danger" data-test="end-session" @click="endCurrent">{{ t('End Session') }}</button>
          <button v-else-if="current && closed" type="button" class="btn" data-test="open-session" @click="openCurrent">{{ t('Open Session') }}</button>
          <button v-if="campaign" type="button" class="btn primary" data-test="new-session" @click="startNew">{{ t('+ Session') }}</button>
          <button type="button" class="btn primary" data-test="new-campaign" @click="newCampaign">{{ t('+ Campaign') }}</button>
        </span>
      </div>

      <!-- what is being asked (a line of its own, so that the one above does not move) -->
      <p v-if="confirming" class="confirm" data-test="confirm">
        <template v-if="confirming === 'end'"><span data-test="end-confirm">{{ t('End “{name}”? It is saved with its date.', { name: current.name }) }}</span>
          <button type="button" class="btn danger sure" data-test="end-yes" @click="endCurrent">{{ t('End') }}</button></template>
        <template v-else-if="confirming === 'delete'"><span data-test="delete-confirm">{{ t('Delete the session “{name}” for good, with everything on it?', { name: current.name }) }}</span>
          <button type="button" class="btn danger sure" data-test="delete-yes" @click="deleteCurrent">{{ t('Delete') }}</button></template>
        <template v-else-if="confirming === 'new'"><span data-test="new-confirm">{{ t('This ends the active session first.') }}</span>
          <button type="button" class="btn primary sure" data-test="new-yes" @click="startNew">{{ t('Start a new one') }}</button></template>
        <template v-else-if="confirming === 'delete-campaign'"><span data-test="delete-campaign-confirm">{{ campaign.sessions === 1 ? t('Delete the campaign “{name}” and its 1 session for good?', { name: campaign.name }) : t('Delete the campaign “{name}” and its {n} sessions for good?', { name: campaign.name, n: campaign.sessions }) }}</span>
          <button type="button" class="btn danger sure" data-test="delete-campaign-yes" @click="deleteCampaign">{{ t('Delete') }}</button></template>
        <button type="button" class="btn" data-test="confirm-cancel" @click="confirming = ''">{{ t('Cancel') }}</button>
      </p>

      <MasterBoard v-if="campaign && current" :key="current.id" :board="sessions.boardFor(current.id)" :token="token" in-session />
      <p v-else-if="!campaign" class="empty-sessions muted" data-test="campaigns-empty">
        {{ t('A campaign is what you play: it has the characters that play it and a session for each game. Start one with “+ Campaign”, assign its characters, and then start its sessions.') }}
      </p>
      <p v-else class="empty-sessions muted" data-test="sessions-empty">
        {{ t('A session is a board for one game: what happened, what they found. Start one with “+ Session”, then send things to it from the Master Screen with the → on each card.') }}
      </p>
    </template>
  </div>
</template>

<style scoped>
.parts { display: flex; align-items: center; gap: 6px; margin: 0 0 8px; }
.shifts { margin-left: auto; display: inline-flex; align-items: center; gap: 6px; }
.shifts-label { font-size: 0.85rem; font-weight: 700; color: var(--green-dark); margin-right: 2px; }
.shift { width: 1.15rem; height: 1.15rem; padding: 0; border-radius: 50%; border: 2px solid var(--green-dark); background: transparent; cursor: pointer; }
.shift.on { background: var(--green-dark); }
.shifts-reset { margin-left: 4px; padding: 2px 10px; border: 1px solid var(--green-dark); border-radius: 12px; background: none; color: var(--green-dark); font: inherit; font-size: 0.75rem; font-weight: 600; cursor: pointer; }
.shifts-reset:hover { background: var(--parchment); }
.part { padding: 4px 18px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.85rem; font-weight: 600; cursor: pointer; }
.part:hover { background: var(--parchment); }
.part.active { background: var(--green-dark); color: #f3ead2; }
.bar { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; margin: 0 0 8px; font-size: 0.85rem; }
.group { display: inline-flex; align-items: center; gap: 4px; min-width: 0; flex: 0 1 auto; }
.pick { font: inherit; font-weight: 400; color: var(--ink); padding: 4px 8px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; min-width: 0; width: 100%; max-width: 230px; }
.pick.name { font-weight: 600; }
select.pick { text-overflow: ellipsis; }
.state { flex: none; margin-left: 4px; }
.badge { display: inline-block; padding: 0 8px; border: 1px solid var(--line); border-radius: 10px; font-size: 0.72rem; font-weight: 700; letter-spacing: 0.04em; text-transform: uppercase; color: var(--brown); }
.badge.on { border-color: var(--green-dark); color: var(--green-dark); }
.actions { margin-left: auto; display: inline-flex; align-items: center; gap: 6px; flex: none; }
@media (min-width: 900px) { .bar { flex-wrap: nowrap; } }                              /* on a wide screen it is one line */
.icon { width: 24px; height: 24px; padding: 0; border: 1px solid var(--line); border-radius: 50%; background: none; color: var(--green-dark); font-size: 0.75rem; line-height: 1; cursor: pointer; flex: none; }
.icon:hover { background: var(--green-dark); color: #f3ead2; }
.icon.danger { color: var(--danger, #c0392b); }
.icon.danger:hover { background: var(--danger, #c0392b); color: #fff; }
.dropdown { position: relative; flex: 0 1 auto; min-width: 0; }
.drop-btn { display: inline-flex; align-items: center; gap: 6px; max-width: 220px; padding: 4px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; color: var(--ink); font: inherit; cursor: pointer; }
.drop-label { color: var(--muted); }
.drop-value { font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-width: 0; }
.drop-panel { position: absolute; z-index: 100; top: calc(100% + 4px); left: 0; min-width: 240px; max-height: 280px; overflow-y: auto; padding: 6px; background: #fbf6e6; border: 1px solid var(--line); border-radius: 8px; box-shadow: 0 6px 18px rgba(0, 0, 0, 0.2); }
.check-row { display: flex; align-items: center; gap: 8px; padding: 4px 6px; border-radius: 6px; cursor: pointer; }
.check-row:hover { background: var(--parchment); }
.btn { padding: 3px 12px; border: 1px solid var(--green-dark); border-radius: 18px; background: none; color: var(--green-dark); font: inherit; font-size: 0.8rem; font-weight: 600; cursor: pointer; white-space: nowrap; }
.btn:hover { background: var(--green-dark); color: #f3ead2; }
.btn.primary { background: var(--green-dark); color: #f3ead2; }
.btn.danger { border-color: var(--danger, #c0392b); color: var(--danger, #c0392b); }
.btn.danger:hover, .btn.danger.sure { background: var(--danger, #c0392b); color: #fff; }
.confirm { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; margin: 0 0 8px; padding: 6px 12px; border: 1px solid var(--line); border-radius: 10px; background: rgba(251, 246, 230, 0.8); font-size: 0.85rem; color: var(--brown); }
.nosessions { white-space: nowrap; }
.empty-sessions { padding: 2rem 1rem; max-width: 520px; line-height: 1.5; }
.error { color: var(--danger, #c0392b); }
.muted { color: var(--muted); }
.small { font-size: 0.78rem; }
</style>
