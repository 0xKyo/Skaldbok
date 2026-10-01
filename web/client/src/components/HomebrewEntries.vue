<script setup>
// One kind of entry of the GM's pack (spells, weapons, a table...), made from the fields the server says that kind has: a list on the left, the
// form on the right. The server decides what each field is (text, a choice, a list, columns...), so every data file of Core is covered by this.
import { computed, ref, watch } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';

const props = defineProps({
  token: { type: String, required: true },
  section: { type: Object, required: true },               // {id, label, fields: [{key, label, type, options?, cols?}]}
  restore: { type: Object, default: null },                // coming back from making something for a field: {form, openIndex, add: {key, name}}
  startNew: { type: Boolean, default: false },             // open on a new entry (to make what another form needs); saving it says so
  prefill: { type: Object, default: null },                // fields of that new entry that are already known
  pack: { type: String, default: '' },                     // the id of another pack to look into (Core...); empty: the GM's own
  packName: { type: String, default: '' },
});
const emit = defineEmits(['request-new', 'made']);

const entries = ref([]);
const dynamicOptions = ref({});           // choices that are the names of a kind of the Reference ("kin"), by kind
const openIndex = ref(null);              // null: nothing open; -1 a new one; otherwise the index being edited
const form = ref({});
const status = ref('');
const failed = ref(false);
const busy = ref(false);
const confirmingDelete = ref(false);
const loadError = ref('');

const path = computed(() => `/gm/homebrew/${props.section.id}`);
const foreign = computed(() => !!props.pack);                  // another pack: to look into; what is made from it goes to the GM's own
const listPath = computed(() => (foreign.value ? `${path.value}?pack=${encodeURIComponent(props.pack)}` : path.value));
const viewOnly = ref(false);                                   // an entry of another pack is open to be read
const origin = ref(null);                                      // ...that one, as listed
const houseRule = ref(null);                                   // {pack, name}: what is being made replaces that card
const canReplace = ref(false);                                 // the kind is a card (a trap or a table cannot be replaced, only copied)
const formOpen = computed(() => openIndex.value !== null);
const current = computed(() => (openIndex.value >= 0 ? entries.value.find((e) => e.index === openIndex.value) : null));

// requirement: "Word, gesture, ingredient (water)" <-> which components, and what the material is
const parseRequirement = (text) => {
  const r = { word: false, gesture: false, material: false, detail: '' };
  for (const part of String(text ?? '').split(/,\s*(?![^()]*\))/).map((s) => s.trim()).filter(Boolean)) {
    if (/^word/i.test(part)) r.word = true;
    else if (/^gesture/i.test(part)) r.gesture = true;
    else {
      r.material = true;
      r.detail = (part.match(/\(([^)]*)\)/)?.[1] ?? part.replace(/^(material|ingredient|focus)\s*/i, '')).trim();
    }
  }
  return r;
};
const composeRequirement = (r) => {
  const parts = [];
  if (r.word) parts.push('Word');
  if (r.gesture) parts.push('gesture');
  if (r.material) parts.push(r.detail.trim() ? `material (${r.detail.trim()})` : 'material');
  const text = parts.join(', ');
  return text.charAt(0).toUpperCase() + text.slice(1);
};

// range: "Touch", "Personal", "10 meters (sphere)" <-> a kind, a number and a shape
const parseRange = (text) => {
  const t = String(text ?? '').trim();
  if (!t) return { kind: '', meters: '', shape: '', other: '' };
  if (/^touch$/i.test(t)) return { kind: 'Touch', meters: '', shape: '', other: '' };
  if (/^personal$/i.test(t)) return { kind: 'Personal', meters: '', shape: '', other: '' };
  const m = t.match(/^(\d+)\s*meters?(?:\s*\((sphere|cone)\))?$/i);
  if (m) return { kind: 'Meters', meters: m[1], shape: (m[2] ?? '').toLowerCase(), other: '' };
  return { kind: 'Other', meters: '', shape: '', other: t };
};
const composeRange = (r) => {
  if (r.kind === 'Meters') return r.meters ? `${r.meters} meters${r.shape ? ` (${r.shape})` : ''}` : '';
  if (r.kind === 'Other') return r.other.trim();
  return r.kind;
};

// a list of names taken from another kind of the Reference is picked; any other list is lines of text
const isPick = (field) => field.type === 'list' && !!field.optionsFrom;

// stored -> form: lists as lines of text, pairs and rows as lists to edit
function toForm(entry) {
  const f = {};
  for (const field of props.section.fields) {
    const v = entry?.[field.key];
    if (field.widget === 'requirement') f[field.key] = parseRequirement(v);
    else if (field.widget === 'range') f[field.key] = parseRange(v);
    else if (isPick(field)) f[field.key] = [...(v ?? [])];
    else if (field.type === 'list') f[field.key] = (v ?? []).join('\n');
    else if (field.type === 'pairs') f[field.key] = Object.entries(v ?? {}).map(([label, value]) => ({ label, value: String(value) }));
    else if (field.type === 'rows') f[field.key] = (v ?? []).map((r) => Object.fromEntries(field.cols.map((c) => [c.key, r[c.key] == null ? '' : String(r[c.key])])));
    else if (field.type === 'bool') f[field.key] = v === true;
    else f[field.key] = v == null ? '' : String(v);
  }
  return f;
}

function toPayload(f) {
  const out = {};
  for (const field of props.section.fields) {
    if (!shown(field)) continue;                           // what is hidden is not sent
    const v = f[field.key];
    if (field.widget === 'requirement') out[field.key] = composeRequirement(v);
    else if (field.widget === 'range') out[field.key] = composeRange(v);
    else if (isPick(field)) out[field.key] = v;
    else if (field.type === 'list') out[field.key] = v.split('\n').map((s) => s.trim()).filter(Boolean);
    else if (field.type === 'pairs') out[field.key] = Object.fromEntries(v.filter((r) => r.label.trim()).map((r) => [r.label.trim(), r.value]));
    else if (field.type === 'rows') out[field.key] = v.filter((r) => Object.values(r).some((x) => String(x).trim()));
    else out[field.key] = v;
  }
  return out;
}

async function load() {
  openIndex.value = null;
  viewOnly.value = false;
  houseRule.value = null;
  status.value = '';
  try {
    const got = await apiGet(listPath.value, props.token);
    entries.value = got.entries ?? [];
    canReplace.value = got.canReplace === true;
    loadError.value = '';
  } catch (e) {
    entries.value = [];
    loadError.value = e instanceof ApiError ? e.message : 'Could not load your entries.';
  }
  if (props.restore) {                                     // back from making a name for a field: the form as it was, with the new name in
    form.value = props.restore.form;
    openIndex.value = props.restore.openIndex;
    const add = props.restore.add;
    if (add && Array.isArray(form.value[add.key]) && !form.value[add.key].includes(add.name)) form.value[add.key].push(add.name);
  } else if (props.startNew) {
    openNew();
    for (const [key, value] of Object.entries(props.prefill ?? {}))
      if (key in form.value) form.value[key] = value;
  }
}
watch(() => [props.section.id, props.pack], load, { immediate: true });

// the choices of a field are its own, or the names of every entry of a kind of the Reference
async function loadOptions() {
  for (const field of props.section.fields) {
    if (!field.optionsFrom || dynamicOptions.value[field.optionsFrom]) continue;
    try {
      const names = ((await apiGet(`/gm/content/${field.optionsFrom}`, props.token)).entries ?? []).map((e) => e.name);
      dynamicOptions.value = { ...dynamicOptions.value, [field.optionsFrom]: names };
    } catch {
      dynamicOptions.value = { ...dynamicOptions.value, [field.optionsFrom]: [''] };
    }
  }
}
watch(() => props.section.id, loadOptions, { immediate: true });

// the fixed choices of the field, then the names of the Reference it asks for (in capitals when the data writes them so)
const optionsOf = (field) => {
  const extra = (field.optionsFrom ? dynamicOptions.value[field.optionsFrom] ?? [] : []).map((n) => (field.optionsUpper ? n.toUpperCase() : n));
  return [...new Set(['', ...(field.options ?? []), ...extra])];
};
const pickable = (field) => optionsOf(field).filter((n) => n && !form.value[field.key].includes(n));
const addPick = (field, name) => { if (name && !form.value[field.key].includes(name)) form.value[field.key].push(name); };
const requestNew = (field) => emit('request-new', { section: field.optionsFrom, key: field.key, form: JSON.parse(JSON.stringify(form.value)), openIndex: openIndex.value, name: form.value.name ?? '' });
const shown = (field) => !field.showWhen || form.value[field.showWhen.key] === field.showWhen.value;

function openNew() {
  form.value = toForm(null);
  openIndex.value = -1;
  status.value = '';
  confirmingDelete.value = false;
}

function openEntry(e) {
  form.value = toForm(e);
  origin.value = e;
  viewOnly.value = foreign.value;
  houseRule.value = null;
  openIndex.value = e.index;
  status.value = '';
  failed.value = false;
  confirmingDelete.value = false;
}

const close = () => { openIndex.value = null; viewOnly.value = false; houseRule.value = null; confirmingDelete.value = false; };

// from an entry of another pack: change it as a house rule (it replaces that card) or take a copy (a new one of your own)
function startOwn(asRule) {
  houseRule.value = asRule ? { pack: props.pack, name: origin.value.name } : null;
  if (!asRule) form.value.name = `${form.value.name} (copy)`;
  viewOnly.value = false;
  openIndex.value = -1;
  status.value = '';
  failed.value = false;
}
const query = (name) => `?name=${encodeURIComponent(name)}`;

async function save() {
  if (!String(form.value.name ?? '').trim() || busy.value) return;
  busy.value = true;
  failed.value = false;
  try {
    const payload = toPayload(form.value);
    const wasRule = !!houseRule.value;
    if (houseRule.value) payload.houseRuleOf = houseRule.value;
    const saved = openIndex.value >= 0
      ? await apiSend('PUT', `${path.value}/${openIndex.value}${query(current.value?.name ?? '')}`, props.token, payload)
      : await apiSend('POST', path.value, props.token, payload);
    const index = saved.index;
    entries.value = (await apiGet(listPath.value, props.token)).entries ?? [];
    if (foreign.value) {                                   // made from an entry of another pack: it is in the GM's own pack now, and the list says so
      openIndex.value = null;
      houseRule.value = null;
      status.value = wasRule ? 'Saved as a house rule in your pack. It replaces the original now.' : 'Saved in your pack.';
      return;
    }
    openIndex.value = index;
    status.value = 'Saved. It is in the Rules now.';
    if (props.startNew) emit('made', payload.name);
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not save it.';
  } finally {
    busy.value = false;
  }
}

async function remove() {
  if (openIndex.value < 0 || !current.value) return;
  try {
    await apiSend('DELETE', `${path.value}/${openIndex.value}${query(current.value.name)}`, props.token);
    await load();
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not delete it.';
    confirmingDelete.value = false;
  }
}

const addPair = (list) => list.push({ label: '', value: '' });
const addRow = (field) => form.value[field.key].push(Object.fromEntries(field.cols.map((c) => [c.key, ''])));
const removeAt = (list, i) => list.splice(i, 1);
</script>

<template>
  <div class="layout" :class="{ 'form-open': formOpen }" data-test="entries">
    <aside class="list" :aria-label="foreign ? `${packName}: ${section.label}` : `Your ${section.label}`">
      <p v-if="loadError" class="error">{{ loadError }}</p>
      <div class="list-actions">
        <button v-if="!foreign" type="button" class="pill" data-test="new-entry" @click="openNew">+ New</button>
        <span v-else class="lock muted" data-test="read-only">🔒 {{ packName }}: read only. Open one to house-rule it.</span>
      </div>
      <p v-if="status && !formOpen" class="status" data-test="list-status">{{ status }}</p>
      <p v-if="!entries.length" class="muted">Nothing {{ foreign ? 'in this pack' : 'yet' }} in {{ section.label }}.</p>
      <button v-for="e in entries" :key="e.index" type="button" class="row" :class="{ active: openIndex === e.index }" data-test="entry-row" @click="openEntry(e)">
        <span class="row-name">{{ e.name }}</span>
        <span v-if="e.houseRuled" class="mark" data-test="house-ruled">house-ruled</span>
        <span v-else-if="e.replaces" class="mark" data-test="house-rule-mark">house rule</span>
      </button>
    </aside>

    <form v-if="formOpen" class="form panel" data-test="entry-form" @submit.prevent="save">
      <button type="button" class="back link" @click="close">← Back</button>
      <h2>{{ viewOnly ? packName : houseRule ? 'House rule' : openIndex >= 0 ? 'Edit' : 'New' }} · {{ section.label }}</h2>
      <p v-if="viewOnly" class="note" data-test="view-note">🔒 This is in {{ packName }} and cannot be changed here.
        <template v-if="origin?.houseRuled">You already have a house rule for it: it is in your pack.</template></p>
      <p v-else-if="houseRule" class="note house" data-test="house-note">House rule: what you save here <b>replaces</b> «{{ houseRule.name }}» of {{ packName }} in the Rules, for everybody, and is marked as changed by your pack. The original is not touched.</p>
      <p v-else-if="current?.replaces" class="note house" data-test="replaces-note">House rule: this replaces <code>{{ current.replaces }}</code>.</p>
      <fieldset class="plain" :disabled="viewOnly">

      <template v-for="field in section.fields.filter(shown)" :key="field.key">
        <fieldset v-if="field.widget === 'requirement'" class="group" :data-test="`f-${field.key}`">
          <legend>{{ field.label }}</legend>
          <label class="check"><input v-model="form[field.key].word" type="checkbox" data-test="req-word" /> Word</label>
          <label class="check"><input v-model="form[field.key].gesture" type="checkbox" data-test="req-gesture" /> Gesture</label>
          <label class="check"><input v-model="form[field.key].material" type="checkbox" data-test="req-material" /> Material</label>
          <label v-if="form[field.key].material">Material: what is needed
            <input v-model="form[field.key].detail" maxlength="100" placeholder="water, a holy symbol…" data-test="req-detail" />
          </label>
        </fieldset>
        <fieldset v-else-if="field.widget === 'range'" class="group" :data-test="`f-${field.key}`">
          <legend>{{ field.label }}</legend>
          <select v-model="form[field.key].kind" data-test="range-kind"><option v-for="k in ['', 'Touch', 'Personal', 'Meters', 'Other']" :key="k" :value="k">{{ k || '—' }}</option></select>
          <template v-if="form[field.key].kind === 'Meters'">
            <input v-model="form[field.key].meters" inputmode="numeric" placeholder="How many meters" aria-label="Meters" data-test="range-meters" />
            <select v-model="form[field.key].shape" aria-label="Area" data-test="range-shape"><option v-for="s in ['', 'sphere', 'cone']" :key="s" :value="s">{{ s || 'a single target' }}</option></select>
          </template>
          <input v-if="form[field.key].kind === 'Other'" v-model="form[field.key].other" maxlength="100" placeholder="Describe it" aria-label="Range" data-test="range-other" />
        </fieldset>
        <label v-else-if="field.type === 'text' || field.type === 'number'">{{ field.label }}
          <input v-model="form[field.key]" :inputmode="field.type === 'number' ? 'numeric' : undefined" maxlength="200" :required="field.key === 'name'" :data-test="`f-${field.key}`" />
        </label>
        <label v-else-if="field.type === 'long'">{{ field.label }}
          <textarea v-model="form[field.key]" rows="5" maxlength="4000" :data-test="`f-${field.key}`"></textarea>
        </label>
        <label v-else-if="field.type === 'select'">{{ field.label }}
          <select v-model="form[field.key]" :data-test="`f-${field.key}`"><option v-for="o in optionsOf(field)" :key="o" :value="o">{{ o || '—' }}</option></select>
        </label>
        <label v-else-if="field.type === 'bool'" class="check">
          <input v-model="form[field.key]" type="checkbox" :data-test="`f-${field.key}`" /> {{ field.label }}
        </label>
        <div v-else-if="isPick(field)" class="group" :data-test="`f-${field.key}`">
          <h3>{{ field.label }}</h3>
          <ul class="picked">
            <li v-for="(n, i) in form[field.key]" :key="n" data-test="picked">{{ n }} <button type="button" class="x" :aria-label="`Remove ${n}`" @click="removeAt(form[field.key], i)">×</button></li>
          </ul>
          <div class="pair">
            <select aria-label="Add" data-test="pick-add" @change="addPick(field, $event.target.value); $event.target.value = ''">
              <option value="">Add…</option>
              <option v-for="n in pickable(field)" :key="n" :value="n">{{ n }}</option>
            </select>
            <button type="button" class="pill small" data-test="pick-new" @click="requestNew(field)">+ New…</button>
          </div>
        </div>
        <label v-else-if="field.type === 'list'">{{ field.label }} <span class="muted">(one per line)</span>
          <textarea v-model="form[field.key]" rows="3" :data-test="`f-${field.key}`"></textarea>
        </label>
        <div v-else-if="field.type === 'pairs'" class="group" :data-test="`f-${field.key}`">
          <h3>{{ field.label }}</h3>
          <div v-for="(p, i) in form[field.key]" :key="i" class="pair" data-test="pair-row">
            <input v-model="p.label" maxlength="40" placeholder="Label" aria-label="Label" />
            <input v-model="p.value" maxlength="1000" placeholder="Value" aria-label="Value" />
            <button type="button" class="x" aria-label="Remove" @click="removeAt(form[field.key], i)">×</button>
          </div>
          <button type="button" class="pill small" data-test="add-pair" @click="addPair(form[field.key])">+ Column</button>
        </div>
        <div v-else-if="field.type === 'rows'" class="group" :data-test="`f-${field.key}`">
          <h3>{{ field.label }}</h3>
          <div v-for="(r, i) in form[field.key]" :key="i" class="pair" data-test="row-row">
            <input v-for="c in field.cols" :key="c.key" v-model="r[c.key]" :placeholder="c.label" :aria-label="c.label" />
            <button type="button" class="x" aria-label="Remove" @click="removeAt(form[field.key], i)">×</button>
          </div>
          <button type="button" class="pill small" data-test="add-row" @click="addRow(field)">+ Row</button>
        </div>
      </template>
      </fieldset>

      <div v-if="viewOnly" class="actions">
        <button v-if="canReplace && !origin?.houseRuled" type="button" class="pill primary" data-test="house-rule-it" @click="startOwn(true)">House rule it</button>
        <button type="button" class="pill" data-test="copy-it" @click="startOwn(false)">Copy to my pack</button>
        <button type="button" class="pill" @click="close">Close</button>
      </div>
      <div v-else class="actions">
        <button type="submit" class="pill primary" :disabled="!String(form.name ?? '').trim() || busy" data-test="save-entry">Save</button>
        <button type="button" class="pill" @click="close">Close</button>
        <template v-if="openIndex >= 0 && !foreign">
          <button v-if="!confirmingDelete" type="button" class="pill danger" data-test="delete-entry" @click="confirmingDelete = true">Delete</button>
          <span v-else class="confirm" data-test="delete-confirm">Delete «{{ form.name }}»?
            <button type="button" class="pill danger sure" data-test="delete-yes" @click="remove">Delete</button>
            <button type="button" class="pill" @click="confirmingDelete = false">Cancel</button>
          </span>
        </template>
        <span v-if="status" class="status" :class="{ error: failed }" data-test="status">{{ status }}</span>
      </div>
    </form>
    <p v-else class="muted hint">Pick one, or make a new one.</p>
  </div>
</template>

<style scoped>
.layout { display: grid; grid-template-columns: 1fr; gap: 14px; align-items: start; }
@media (min-width: 720px) { .layout { grid-template-columns: minmax(220px, 300px) 1fr; } .back { display: none; } }
@media (max-width: 719px) { .layout.form-open .list { display: none; } }
.form h2 { margin: 0 0 8px; font-size: 1.1rem; color: var(--green-dark); }
.form h3 { margin: 10px 0 6px; font-size: 0.85rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--brown); }
.list-actions { display: flex; gap: 6px; margin-bottom: 10px; }
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
.row-name { font-weight: 600; }
.form { display: flex; flex-direction: column; gap: 10px; }
.form label { display: flex; flex-direction: column; gap: 3px; font-size: 0.85rem; color: var(--muted); }
.form label.check { flex-direction: row; align-items: center; gap: 8px; }
.form label.check input { width: auto; }
.form input, .form select, .form textarea { font: inherit; color: var(--ink); padding: 6px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; width: 100%; box-sizing: border-box; }
.group legend { font-size: 0.85rem; color: var(--muted); padding: 0 4px; }
.group { display: flex; flex-direction: column; gap: 6px; padding: 8px; border: 1px solid var(--line); border-radius: 8px; }
.plain { border: 0; padding: 0; margin: 0; min-width: 0; display: flex; flex-direction: column; gap: 10px; }
.note { margin: 0; padding: 6px 10px; border-radius: 8px; background: var(--parchment); font-size: 0.85rem; }
.note.house { border-left: 3px solid var(--brown); }
.mark { font-size: 0.7rem; letter-spacing: 0.06em; text-transform: uppercase; color: var(--brown); align-self: center; }
.row.active .mark { color: #f3ead2; }
.lock { font-size: 0.8rem; }
.picked { list-style: none; margin: 0 0 6px; padding: 0; display: flex; flex-wrap: wrap; gap: 6px; }
.picked li { display: inline-flex; align-items: center; gap: 2px; padding: 2px 4px 2px 10px; border: 1px solid var(--line); border-radius: 14px; background: var(--cream); font-size: 0.85rem; }
.pair { display: flex; gap: 6px; margin-bottom: 4px; }
.x { border: 0; background: none; color: var(--muted); font-size: 1.1rem; cursor: pointer; }
.x:hover { color: var(--danger, #c0392b); }
.actions { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; margin-top: 10px; }
.confirm { display: inline-flex; align-items: center; gap: 8px; flex-wrap: wrap; color: var(--danger, #c0392b); font-size: 0.85rem; }
.status { font-size: 0.85rem; color: var(--green-dark); }
.status.error, .error { color: var(--danger, #c0392b); }
.muted { color: var(--muted); }
.hint { padding: 1rem; }
</style>
