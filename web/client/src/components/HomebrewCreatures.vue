<script setup>
// The GM's homebrew creatures, kept in the GM's own pack and shown in the Reference like any other. A list of what
// they made on the left and the form on the right; a new creature can start from any creature of the books (a copy to change).
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';

const props = defineProps({
  token: { type: String, required: true },
});

const STAT_LABELS = ['Ferocity', 'Size', 'Movement', 'Armor', 'HP'];
const KINDS = [{ id: 'monster', label: 'Monster' }, { id: 'npc', label: 'NPC' }, { id: 'animal', label: 'Animal' }];
const DICE = ['D4', 'D6', 'D8', 'D10', 'D12', 'D20'];

const blank = () => ({
  name: '', kind: 'monster', category: '', quote: '', description: '', attack_dice: 'D6',
  statblocks: [{ variant: '', fields: STAT_LABELS.map((label) => ({ label, value: '' })) }],
  attacks: [], abilities: [], adventure_seed: '', random_encounter: '',
});

const creatures = ref([]);            // the GM's own, as stored
const editingId = ref(null);          // null: nothing open; '' a new one; otherwise the id being edited
const form = ref(blank());
const status = ref('');
const failed = ref(false);
const busy = ref(false);
const confirmingDelete = ref(false);
const loadError = ref('');

// start from a creature of the books
const copying = ref(false);
const copyQuery = ref('');
const copyResults = ref([]);
let copyTimer = null;

const formOpen = computed(() => editingId.value !== null);

async function load() {
  try {
    creatures.value = (await apiGet('/gm/homebrew/creatures', props.token)).creatures ?? [];
    loadError.value = '';
  } catch (e) {
    loadError.value = e instanceof ApiError ? e.message : 'Could not load your creatures.';
  }
}
onMounted(load);
onBeforeUnmount(() => clearTimeout(copyTimer));

// ---- stored <-> form ----
function toForm(c) {
  const f = blank();
  f.name = c.name ?? '';
  f.kind = c.kind ?? 'monster';
  f.category = c.category ?? '';
  f.quote = c.quote ?? '';
  f.description = Array.isArray(c.description) ? c.description.join('\n') : (c.description ?? '');
  f.attack_dice = c.attack_dice ?? 'D6';
  f.adventure_seed = c.adventure_seed ?? '';
  f.random_encounter = c.random_encounter ?? '';
  f.statblocks = (c.statblocks ?? []).map((b) => ({ variant: b.variant ?? '', fields: Object.entries(b.fields ?? {}).map(([label, value]) => ({ label, value: String(value) })) }));
  if (!f.statblocks.length) f.statblocks = blank().statblocks;
  f.attacks = (c.attacks ?? []).map((a) => ({ roll: a.roll ?? '', name: a.name ?? '', text: a.text ?? '' }));
  f.abilities = (c.abilities ?? []).map((a) => ({ name: a.name ?? '', text: a.text ?? '' }));
  return f;
}

// A creature of the Reference (its detail view) as a form to change: the text of an attack already begins with its name.
function fromDetail(d) {
  const strip = (name, text) => (name && text.startsWith(name) ? text.slice(name.length).trim() : text);
  return toForm({
    name: d.name,
    kind: d.kind,
    category: d.category,
    quote: d.quote,
    description: d.description,
    adventure_seed: d.adventureSeed,
    random_encounter: d.randomEncounter,
    statblocks: d.blocks.map((b) => ({ variant: b.variant, fields: Object.fromEntries(b.fields.map((f) => [f.label, f.value])) })),
    attacks: d.attacks.map((a) => ({ roll: a.rollText, name: a.name, text: strip(a.name, a.text) })),
    abilities: d.abilities.map((a) => ({ name: a.name, text: a.text })),
  });
}

function toPayload(f) {
  return {
    name: f.name.trim(), kind: f.kind, category: f.category.trim(), quote: f.quote, description: f.description, attack_dice: f.attack_dice,
    adventure_seed: f.adventure_seed, random_encounter: f.random_encounter,
    statblocks: f.statblocks
      .map((b) => ({ variant: b.variant.trim(), fields: Object.fromEntries(b.fields.filter((r) => r.label.trim()).map((r) => [r.label.trim(), r.value])) }))
      .filter((b) => Object.keys(b.fields).length),
    attacks: f.attacks.filter((a) => a.name.trim() || a.text.trim()).map((a) => ({ roll: a.roll.trim(), name: a.name.trim(), text: a.text })),
    abilities: f.abilities.filter((a) => a.name.trim() || a.text.trim()).map((a) => ({ name: a.name.trim(), text: a.text })),
  };
}

// ---- opening, saving, deleting ----
function openNew() {
  form.value = blank();
  editingId.value = '';
  status.value = '';
  confirmingDelete.value = false;
  copying.value = false;
}

function openCreature(c) {
  form.value = toForm(c);
  editingId.value = c.id;
  status.value = '';
  failed.value = false;
  confirmingDelete.value = false;
  copying.value = false;
}

function close() {
  editingId.value = null;
  confirmingDelete.value = false;
}

async function save() {
  if (!form.value.name.trim() || busy.value) return;
  busy.value = true;
  failed.value = false;
  try {
    const payload = toPayload(form.value);
    const saved = editingId.value
      ? await apiSend('PUT', `/gm/homebrew/creatures/${editingId.value}`, props.token, payload)
      : await apiSend('POST', '/gm/homebrew/creatures', props.token, payload);
    editingId.value = saved.id;
    status.value = 'Saved. It is in the Rules now, under Creatures.';
    await load();
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not save the creature.';
  } finally {
    busy.value = false;
  }
}

async function remove() {
  if (!editingId.value) return;
  try {
    await apiSend('DELETE', `/gm/homebrew/creatures/${editingId.value}`, props.token);
    close();
    await load();
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not delete the creature.';
    confirmingDelete.value = false;
  }
}

// ---- starting from a creature of the books ----
async function searchCopy() {
  try {
    const q = copyQuery.value.trim();
    copyResults.value = ((await apiGet(`/gm/creatures${q ? `?q=${encodeURIComponent(q)}` : ''}`, props.token)).creatures ?? []).slice(0, 40);
  } catch {
    copyResults.value = [];
  }
}
watch([copying, copyQuery], () => {
  clearTimeout(copyTimer);
  if (copying.value) copyTimer = setTimeout(searchCopy, 200);
});

async function copyFrom(item) {
  try {
    const d = await apiGet(`/gm/creatures/${encodeURIComponent(item.key)}`, props.token);
    form.value = fromDetail(d);
    form.value.name = `${d.name} (copy)`;
    editingId.value = '';
    copying.value = false;
    status.value = `A copy of ${d.name}: change what you want and save it.`;
    failed.value = false;
  } catch {
    status.value = 'Could not open that creature.';
    failed.value = true;
  }
}

// ---- rows of the form ----
const addBlock = () => form.value.statblocks.push({ variant: '', fields: [{ label: '', value: '' }] });
const removeAt = (list, i) => list.splice(i, 1);
</script>

<template>
  <section class="homebrew" data-test="homebrew">
    <p v-if="loadError" class="error">{{ loadError }}</p>

    <div class="layout" :class="{ 'form-open': formOpen }">
      <aside class="list" aria-label="Your creatures">
        <h2>Your creatures</h2>
        <div class="list-actions">
          <button type="button" class="pill" data-test="new-creature" @click="openNew">+ New creature</button>
          <button type="button" class="pill" data-test="copy-creature" :aria-expanded="copying" @click="copying = !copying">Start from a creature…</button>
        </div>

        <div v-if="copying" class="copy panel" data-test="copy-panel">
          <input v-model="copyQuery" type="search" placeholder="Search the creatures…" aria-label="Search the creatures" data-test="copy-search" />
          <ul>
            <li v-for="r in copyResults" :key="r.key">
              <button type="button" data-test="copy-result" @click="copyFrom(r)"><b>{{ r.name }}</b> <span class="muted">{{ r.sub }}</span></button>
            </li>
          </ul>
        </div>

        <p v-if="!creatures.length" class="muted">Nothing yet. Make one, or start from a creature of the books.</p>
        <button v-for="c in creatures" :key="c.id" type="button" class="row" :class="{ active: editingId === c.id }" data-test="creature-row" @click="openCreature(c)">
          <span class="row-name">{{ c.name }}</span><span class="muted">{{ c.category || c.kind }}</span>
        </button>
      </aside>

      <form v-if="formOpen" class="form panel" data-test="creature-form" @submit.prevent="save">
        <button type="button" class="back link" data-test="form-back" @click="close">← Back</button>
        <h2>{{ editingId ? 'Edit creature' : 'New creature' }}</h2>

        <div class="grid2">
          <label>Name<input v-model="form.name" maxlength="80" required data-test="f-name" /></label>
          <label>Type
            <select v-model="form.kind" data-test="f-kind"><option v-for="k in KINDS" :key="k.id" :value="k.id">{{ k.label }}</option></select>
          </label>
          <label>Category<input v-model="form.category" maxlength="60" placeholder="Undead, Trolls…" data-test="f-category" /></label>
          <label>Attack die
            <select v-model="form.attack_dice" data-test="f-dice"><option v-for="d in DICE" :key="d" :value="d">{{ d }}</option></select>
          </label>
        </div>
        <label>Quote<textarea v-model="form.quote" rows="2" maxlength="600"></textarea></label>
        <label>Description<textarea v-model="form.description" rows="5" maxlength="4000" data-test="f-description"></textarea></label>

        <h3>Stat blocks</h3>
        <div v-for="(b, bi) in form.statblocks" :key="bi" class="block" data-test="stat-block">
          <div class="block-head">
            <input v-model="b.variant" maxlength="40" placeholder="Variant (Scout, Warrior… optional)" aria-label="Variant" />
            <button v-if="form.statblocks.length > 1" type="button" class="x" :aria-label="`Remove stat block ${bi + 1}`" @click="removeAt(form.statblocks, bi)">×</button>
          </div>
          <div v-for="(f, fi) in b.fields" :key="fi" class="pair" data-test="stat-field">
            <input v-model="f.label" maxlength="40" placeholder="Label" aria-label="Stat label" />
            <input v-model="f.value" maxlength="200" placeholder="Value" aria-label="Stat value" />
            <button type="button" class="x" aria-label="Remove field" @click="removeAt(b.fields, fi)">×</button>
          </div>
          <button type="button" class="pill small" data-test="add-field" @click="b.fields.push({ label: '', value: '' })">+ Field</button>
        </div>
        <button type="button" class="pill small" data-test="add-block" @click="addBlock">+ Stat block</button>

        <h3>Attacks</h3>
        <div v-for="(a, ai) in form.attacks" :key="ai" class="attack" data-test="attack-row">
          <input v-model="a.roll" maxlength="20" placeholder="Roll (1-2)" aria-label="Attack roll" class="roll" />
          <input v-model="a.name" maxlength="80" placeholder="Attack" aria-label="Attack name" />
          <textarea v-model="a.text" rows="2" maxlength="1000" placeholder="What it does" aria-label="Attack text"></textarea>
          <button type="button" class="x" :aria-label="`Remove attack ${ai + 1}`" @click="removeAt(form.attacks, ai)">×</button>
        </div>
        <button type="button" class="pill small" data-test="add-attack" @click="form.attacks.push({ roll: '', name: '', text: '' })">+ Attack</button>

        <h3>Abilities</h3>
        <div v-for="(a, ai) in form.abilities" :key="ai" class="attack" data-test="ability-row">
          <input v-model="a.name" maxlength="80" placeholder="Ability" aria-label="Ability name" />
          <textarea v-model="a.text" rows="2" maxlength="1000" placeholder="What it does" aria-label="Ability text"></textarea>
          <button type="button" class="x" :aria-label="`Remove ability ${ai + 1}`" @click="removeAt(form.abilities, ai)">×</button>
        </div>
        <button type="button" class="pill small" data-test="add-ability" @click="form.abilities.push({ name: '', text: '' })">+ Ability</button>

        <label>Adventure seed<textarea v-model="form.adventure_seed" rows="2" maxlength="1000"></textarea></label>
        <label>Random encounter<textarea v-model="form.random_encounter" rows="2" maxlength="1000"></textarea></label>

        <div class="actions">
          <button type="submit" class="pill primary" :disabled="!form.name.trim() || busy" data-test="save-creature">Save</button>
          <button type="button" class="pill" @click="close">Close</button>
          <template v-if="editingId">
            <button v-if="!confirmingDelete" type="button" class="pill danger" data-test="delete-creature" @click="confirmingDelete = true">Delete</button>
            <span v-else class="confirm" data-test="delete-confirm">Delete «{{ form.name }}»?
              <button type="button" class="pill danger sure" data-test="delete-yes" @click="remove">Delete</button>
              <button type="button" class="pill" @click="confirmingDelete = false">Cancel</button>
            </span>
          </template>
          <span v-if="status" class="status" :class="{ error: failed }" data-test="status">{{ status }}</span>
        </div>
      </form>
      <p v-else class="muted hint">Pick one of your creatures, or make a new one.</p>
    </div>
  </section>
</template>

<style scoped>
.layout { display: grid; grid-template-columns: 1fr; gap: 14px; align-items: start; }
@media (min-width: 720px) { .layout { grid-template-columns: minmax(220px, 300px) 1fr; } .back { display: none; } }
@media (max-width: 719px) { .layout.form-open .list { display: none; } }
.list h2, .form h2 { margin: 0 0 8px; font-size: 1.1rem; color: var(--green-dark); }
.form h3 { margin: 16px 0 6px; font-size: 0.85rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--brown); }
.list-actions { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 10px; }
.pill { padding: 4px 14px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.85rem; font-weight: 600; cursor: pointer; }
.pill:hover:not(:disabled) { background: var(--green-dark); color: #f3ead2; }
.pill:disabled { opacity: 0.45; cursor: default; }
.pill.small { padding: 2px 10px; font-size: 0.8rem; margin-top: 4px; align-self: flex-start; }
.pill.primary { background: var(--green-dark); color: #f3ead2; }
.pill.danger { border-color: var(--danger, #c0392b); color: var(--danger, #c0392b); }
.pill.danger:hover { background: var(--danger, #c0392b); color: #fff; }
.pill.sure { background: var(--danger, #c0392b); color: #fff; }
.row { display: flex; justify-content: space-between; gap: 6px; width: 100%; padding: 8px 12px; border: 0; border-bottom: 1px solid var(--line); background: var(--cream); font: inherit; color: var(--ink); text-align: left; cursor: pointer; }
.row:hover { background: var(--parchment); }
.row.active { background: var(--green-dark); color: #f3ead2; }
.row.active .muted { color: rgba(243, 234, 210, 0.7); }
.row-name { font-weight: 600; }
.copy { margin-bottom: 10px; padding: 8px; }
.copy input { width: 100%; box-sizing: border-box; font: inherit; padding: 6px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; }
.copy ul { list-style: none; margin: 6px 0 0; padding: 0; max-height: 220px; overflow-y: auto; }
.copy li button { width: 100%; text-align: left; padding: 5px 8px; border: 0; border-bottom: 1px solid var(--line); background: none; font: inherit; cursor: pointer; }
.copy li button:hover { background: var(--parchment); }
.form { display: flex; flex-direction: column; gap: 10px; }
.form label { display: flex; flex-direction: column; gap: 3px; font-size: 0.85rem; color: var(--muted); }
.form input, .form select, .form textarea { font: inherit; color: var(--ink); padding: 6px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; width: 100%; box-sizing: border-box; }
.grid2 { display: grid; grid-template-columns: 1fr; gap: 10px; }
@media (min-width: 560px) { .grid2 { grid-template-columns: 1fr 1fr; } }
.block { padding: 8px; border: 1px solid var(--line); border-radius: 8px; margin-bottom: 6px; }
.block-head, .pair { display: grid; grid-template-columns: 1fr 1fr auto; gap: 6px; margin-bottom: 4px; }
.block-head { grid-template-columns: 1fr auto; }
.attack { display: grid; grid-template-columns: 6rem 1fr auto; gap: 6px; margin-bottom: 6px; align-items: start; }
.attack textarea { grid-column: 1 / 3; }
.attack .roll { min-width: 0; }
@media (max-width: 559px) { .attack { grid-template-columns: 1fr auto; } .attack textarea { grid-column: 1 / 2; } }
.x { border: 0; background: none; color: var(--muted); font-size: 1.1rem; cursor: pointer; }
.x:hover { color: var(--danger, #c0392b); }
.actions { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; margin-top: 10px; }
.confirm { display: inline-flex; align-items: center; gap: 8px; flex-wrap: wrap; color: var(--danger, #c0392b); font-size: 0.85rem; }
.status { font-size: 0.85rem; color: var(--green-dark); }
.status.error, .error { color: var(--danger, #c0392b); }
.muted { color: var(--muted); }
.hint { padding: 1rem; }
</style>
