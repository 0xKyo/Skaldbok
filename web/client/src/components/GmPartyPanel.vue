<script setup>
// The GM's page of one party: its name, its members, "Add to Party" (any character not in it yet: a character can be in several
// parties) and a message to the whole party, which lands in each member's conversation as a broadcast.
import { computed, ref } from 'vue';
import { ApiError, apiSend } from '../api.js';

const props = defineProps({
  party: { type: Object, required: true },        // {id, name, members[] (character ids)}
  characters: { type: Array, required: true },    // every character: {id, name, kin, profession, hp, wp}
  token: { type: String, required: true },
});
const emit = defineEmits(['changed', 'deleted', 'open-character']);

const members = computed(() => props.party.members.map((id) => props.characters.find((c) => c.id === id)).filter(Boolean));
const available = computed(() => props.characters.filter((c) => !props.party.members.includes(c.id)));

const picking = ref(false);
const confirmingDelete = ref(false);
const text = ref('');
const status = ref('');
const failed = ref(false);
const busy = ref(false);

async function change(payload) {
  try {
    await apiSend('PATCH', `/gm/parties/${props.party.id}`, props.token, payload);
    emit('changed');
  } catch (e) {
    status.value = e.message ?? 'Could not change the party.';
    failed.value = true;
  }
}

const renaming = ref(false);
const newName = ref('');

function startRename() {
  newName.value = props.party.name;
  renaming.value = true;
}

async function rename() {
  const name = newName.value.trim();
  if (!name) return;
  if (name !== props.party.name) await change({ name });
  renaming.value = false;
}

async function add(id) {
  picking.value = false;
  await change({ addMember: id });
}

// deleting a party keeps its characters: they only stop being members
async function removeParty() {
  try {
    await apiSend('DELETE', `/gm/parties/${props.party.id}`, props.token);
    emit('deleted', props.party.id);
  } catch (e) {
    confirmingDelete.value = false;
    failed.value = true;
    status.value = e.message ?? 'Could not delete the party.';
  }
}

async function send() {
  const message = text.value.trim();
  if (!message || busy.value) return;
  busy.value = true;
  failed.value = false;
  try {
    const res = await apiSend('POST', `/gm/parties/${props.party.id}/message`, props.token, { text: message });
    text.value = '';
    status.value = `Sent to ${res.sent} character${res.sent === 1 ? '' : 's'}.`;
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not send the message.';
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <section class="party-panel" data-test="party-panel">
    <header class="party-head">
      <form v-if="renaming" class="rename" @submit.prevent="rename">
        <input v-model="newName" maxlength="80" aria-label="Party name" data-test="rename-input" @keydown.esc="renaming = false" />
        <button type="submit" class="del-btn" :disabled="!newName.trim()" data-test="rename-save">Save</button>
        <button type="button" class="del-btn" data-test="rename-cancel" @click="renaming = false">Cancel</button>
      </form>
      <template v-else>
        <h2 class="party-name" data-test="party-name">{{ party.name }}</h2>
        <button type="button" class="del-btn" data-test="rename-party" @click="startRename">Rename</button>
      </template>
      <span class="muted">{{ members.length }} member{{ members.length === 1 ? '' : 's' }}</span>
      <button type="button" class="add-btn" data-test="add-to-party" @click="picking = !picking">Add to Party</button>
      <button v-if="!confirmingDelete" type="button" class="del-btn" data-test="delete-party" @click="confirmingDelete = true">Delete party</button>
      <span v-else class="confirm" data-test="delete-confirm">Delete «{{ party.name }}»? Its characters are kept.
        <button type="button" class="del-btn sure" data-test="delete-yes" @click="removeParty">Delete</button>
        <button type="button" class="del-btn" data-test="delete-no" @click="confirmingDelete = false">Cancel</button>
      </span>
    </header>

    <div v-if="picking" class="picker panel" data-test="party-picker">
      <p v-if="!available.length" class="muted">Every character is already in this party.</p>
      <button v-for="c in available" :key="c.id" type="button" class="pick" data-test="party-pick" @click="add(c.id)">
        <b>{{ c.name || c.id }}</b> <span class="muted">{{ c.kin }} {{ c.profession }}</span>
      </button>
    </div>

    <p v-if="!members.length" class="muted">No characters in this party yet.</p>
    <table v-else class="members">
      <thead><tr><th>Name</th><th>Class</th><th>Kin</th><th></th></tr></thead>
      <tbody>
        <tr v-for="c in members" :key="c.id" class="member" data-test="party-member">
          <td><button type="button" class="member-name" @click="emit('open-character', c.id)">{{ c.name || c.id }}</button></td>
          <td>{{ c.profession }}</td>
          <td>{{ c.kin }}</td>
          <td class="end"><button type="button" class="remove" :aria-label="`Remove ${c.name} from the party`" data-test="party-remove" @click="change({ removeMember: c.id })">×</button></td>
        </tr>
      </tbody>
    </table>

    <form class="message panel" @submit.prevent="send">
      <label class="message-label" :for="`party-message-${party.id}`">Message to the whole party</label>
      <textarea :id="`party-message-${party.id}`" v-model="text" rows="3" maxlength="2000" placeholder="Write to every member…" data-test="party-message"></textarea>
      <div class="message-row">
        <button type="submit" class="send-btn" :disabled="!text.trim() || !members.length || busy" data-test="party-send">Send to party</button>
        <span v-if="status" class="status" :class="{ error: failed }" data-test="party-status">{{ status }}</span>
      </div>
    </form>
  </section>
</template>

<style scoped>
.party-panel { padding: 1rem 1.25rem; margin-bottom: 1.25rem; border: 1px solid var(--line); border-radius: var(--radius); }
.party-head { display: flex; align-items: baseline; gap: 12px; flex-wrap: wrap; margin-bottom: 12px; }
.party-name { margin: 0; font-size: 1.25rem; color: var(--green-dark); }
.rename { display: flex; align-items: center; gap: 8px; }
.rename input { font: inherit; font-size: 1.1rem; padding: 3px 8px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; color: var(--ink); }
.add-btn { margin-left: auto; padding: 4px 14px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.85rem; font-weight: 600; cursor: pointer; }
.add-btn:hover { background: var(--green-dark); color: #f3ead2; }
.del-btn { padding: 4px 12px; border: 1px solid var(--line); border-radius: 20px; background: none; color: var(--muted); font: inherit; font-size: 0.8rem; cursor: pointer; }
.del-btn:hover { color: var(--danger, #c0392b); border-color: var(--danger, #c0392b); }
.del-btn.sure { background: var(--danger, #c0392b); border-color: var(--danger, #c0392b); color: #fff; }
.confirm { display: inline-flex; align-items: center; gap: 8px; flex-wrap: wrap; font-size: 0.85rem; color: var(--danger, #c0392b); }
.picker { display: flex; flex-direction: column; gap: 2px; margin-bottom: 12px; padding: 6px; }
.pick { text-align: left; padding: 6px 10px; border: 0; border-radius: 6px; background: none; font: inherit; cursor: pointer; color: var(--ink); }
.pick:hover { background: var(--parchment); }
.members { width: 100%; border-collapse: collapse; margin: 0 0 16px; border: 1px solid var(--line); border-radius: var(--radius); background: var(--cream); }
.members th { text-align: left; padding: 7px 12px; }
.members td { padding: 8px 12px; border-top: 1px solid var(--line); }
.members .end { text-align: right; width: 2rem; }
.member-name { border: 0; background: none; padding: 0; font: inherit; font-weight: 600; color: var(--green-dark); cursor: pointer; text-align: left; }
.member-name:hover { text-decoration: underline; }
.remove { border: 0; background: none; color: var(--muted); font-size: 1.1rem; line-height: 1; cursor: pointer; }
.remove:hover { color: var(--red, #a33); }
.message { display: flex; flex-direction: column; gap: 8px; padding: 12px 14px; }
.message-label { font-family: var(--display); font-weight: 800; font-size: 0.75rem; letter-spacing: 0.1em; text-transform: uppercase; color: var(--brown); }
.message textarea { width: 100%; box-sizing: border-box; font: inherit; padding: 8px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; color: var(--ink); resize: vertical; }
.message-row { display: flex; align-items: center; gap: 12px; }
.send-btn { padding: 6px 18px; border: 1px solid var(--green-dark); border-radius: 20px; background: var(--green-dark); color: #f3ead2; font: inherit; font-weight: 600; cursor: pointer; }
.send-btn:disabled { opacity: 0.45; cursor: default; }
.status { font-size: 0.85rem; color: var(--green-dark); }
.status.error { color: var(--danger, #c0392b); }
.muted { color: var(--muted); }
</style>
