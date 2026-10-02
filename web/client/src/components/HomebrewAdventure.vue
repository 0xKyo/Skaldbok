<script setup>
// Homebrew › Adventure: the GM writes their own adventures. Each one is a pack that the Adventures tab reads like the book's: chapters, and sections inside
// them (to any depth), each with the name the GM chooses, a text, pictures, and creatures taken from the Reference (NPCs and monsters). Saved by itself.
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import { pictureForBoard } from '../lib/pictures.js';
import AdventurePicture from './AdventurePicture.vue';

const props = defineProps({
  token: { type: String, required: true },
});

const KINDS = [
  { id: 'section', label: 'Section' },
  { id: 'location', label: 'Location' },
  { id: 'sidebar', label: 'Sidebar' },
  { id: 'map', label: 'Map' },
  { id: 'text', label: 'Text' },
  { id: 'npc', label: 'NPC' },
  { id: 'monster', label: 'Monster' },
];

const list = ref([]);                 // [{id, title, chapters}]
const chosen = ref('');
const doc = ref(null);                // {id, title, chapters}
const selectedId = ref('');
const naming = ref(false);
const nameDraft = ref('');
const status = ref('');
const failed = ref(false);
const confirming = ref('');           // '' | 'delete-adventure' | 'delete-node'
const loadError = ref('');
const names = ref({});                // creature key -> name, for the chips

let saveTimer = null;
let loading = false;
let counter = 0;
const newId = () => `n${Date.now().toString(36)}${(counter++).toString(36)}${Math.random().toString(36).slice(2, 5)}`;

// ---- the tree ----
function locate(id, nodes = doc.value?.chapters ?? [], parent = null) {
  for (let i = 0; i < nodes.length; i++) {
    if (nodes[i].id === id) return { node: nodes[i], siblings: nodes, index: i, parent };
    const inner = locate(id, nodes[i].sections ?? [], nodes[i]);
    if (inner) return inner;
  }
  return null;
}
const rows = computed(() => {
  const out = [];
  const walk = (nodes, depth) => nodes.forEach((n) => { out.push({ node: n, depth }); walk(n.sections ?? [], depth + 1); });
  walk(doc.value?.chapters ?? [], 0);
  return out;
});
const here = computed(() => (selectedId.value ? locate(selectedId.value) : null));
const node = computed(() => here.value?.node ?? null);
const isChapter = computed(() => !!node.value && !here.value.parent);

function addChapter() {
  const n = { id: newId(), title: `Chapter ${doc.value.chapters.length + 1}`, kind: 'chapter', body: '', sections: [] };
  doc.value.chapters.push(n);
  selectedId.value = n.id;
}
function addChild(kind = 'section', title = 'New section', extra = {}) {
  const parent = node.value;
  if (!parent) return null;
  const n = { id: newId(), title, kind, body: '', sections: [], ...extra };
  (parent.sections ??= []).push(n);
  selectedId.value = n.id;
  return n;
}
function move(delta) {
  const h = here.value;
  if (!h) return;
  const to = h.index + delta;
  if (to < 0 || to >= h.siblings.length) return;
  h.siblings.splice(to, 0, h.siblings.splice(h.index, 1)[0]);
}
const countBelow = (n) => (n.sections ?? []).reduce((sum, k) => sum + 1 + countBelow(k), 0);
function removeNode() {
  const h = here.value;
  if (!h) return;
  h.siblings.splice(h.index, 1);
  selectedId.value = (h.siblings[h.index] ?? h.siblings[h.index - 1] ?? h.parent)?.id ?? '';
  confirming.value = '';
}
function askRemove() {
  if (countBelow(node.value) || node.value.body?.trim()) confirming.value = 'delete-node';
  else removeNode();
}

// ---- loading and saving ----
async function loadList() {
  try {
    list.value = (await apiGet('/gm/homebrew/adventures', props.token)).adventures ?? [];
    loadError.value = '';
  } catch (e) {
    loadError.value = e instanceof ApiError ? e.message : 'Could not load your adventures.';
  }
}
async function open(id) {
  await flush();
  chosen.value = id;
  doc.value = null;
  selectedId.value = '';
  confirming.value = '';
  status.value = '';
  if (!id) return;
  loading = true;
  try {
    const adventure = await apiGet(`/gm/homebrew/adventures/${encodeURIComponent(id)}`, props.token);
    adventure.chapters = adventure.chapters ?? [];
    doc.value = adventure;
    selectedId.value = adventure.chapters[0]?.id ?? '';
    await learnNames();
  } catch (e) {
    loadError.value = e instanceof ApiError ? e.message : 'Could not open that adventure.';
  } finally {
    await nextTick();                                   // what was just loaded is not a change to save
    loading = false;
  }
}
onMounted(async () => {
  await loadList();
  if (list.value.length) await open(list.value[0].id);
});

async function save() {
  clearTimeout(saveTimer);
  saveTimer = null;
  if (!doc.value) return;
  status.value = 'Saving…';
  failed.value = false;
  try {
    await apiSend('PUT', `/gm/homebrew/adventures/${encodeURIComponent(doc.value.id)}`, props.token, { title: doc.value.title, chapters: doc.value.chapters });
    status.value = 'Saved';
    const mine = list.value.find((a) => a.id === doc.value.id);
    if (mine) { mine.title = doc.value.title; mine.chapters = doc.value.chapters.length; }
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not save.';
  }
}
async function flush() {
  if (saveTimer) await save();
}
watch(doc, () => {
  if (loading || !doc.value) return;
  clearTimeout(saveTimer);
  status.value = 'Unsaved changes…';
  saveTimer = setTimeout(save, 800);
}, { deep: true });
onBeforeUnmount(() => { if (saveTimer) save(); });

// ---- the adventures ----
async function create() {
  const title = nameDraft.value.trim();
  if (!title) return;
  try {
    const made = await apiSend('POST', '/gm/homebrew/adventures', props.token, { title });
    naming.value = false;
    nameDraft.value = '';
    await loadList();
    await open(made.id);
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not create the adventure.';
  }
}
async function removeAdventure() {
  const id = chosen.value;
  clearTimeout(saveTimer);
  saveTimer = null;
  try {
    list.value = (await apiSend('DELETE', `/gm/homebrew/adventures/${encodeURIComponent(id)}`, props.token)).adventures ?? [];
    confirming.value = '';
    await open(list.value[0]?.id ?? '');
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not delete the adventure.';
  }
}

// ---- pictures ----
const fileInput = ref(null);
async function upload(event) {
  const file = event.target.files?.[0];
  event.target.value = '';
  if (!file || !node.value) return;
  try {
    const picture = await pictureForBoard(file);
    const kept = await apiSend('POST', `/gm/homebrew/adventures/${encodeURIComponent(doc.value.id)}/images`, props.token, { image: picture.base64 });
    (node.value.images ??= []).push(kept.path);
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : e.message || 'Could not add the picture.';
  }
}
function dropPicture(path) {
  node.value.images = (node.value.images ?? []).filter((p) => p !== path);
}

// ---- creatures: taken from the Reference's list, each one becomes an NPC / monster part of the adventure ----
const creatures = ref([]);            // every creature of the Reference: [{key, name, kind, sub}]
const GROUPS = [{ id: 'npc', label: 'NPCs' }, { id: 'animal', label: 'Animals' }, { id: 'monster', label: 'Monsters' }];
const byKind = computed(() => GROUPS.map((g) => ({
  ...g,
  list: creatures.value.filter((c) => (GROUPS.some((x) => x.id === c.kind) ? c.kind : 'monster') === g.id).sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' })),
})).filter((g) => g.list.length));
onMounted(async () => {
  try {
    creatures.value = (await apiGet('/gm/creatures', props.token)).creatures ?? [];
  } catch {
    creatures.value = [];
  }
});
function addCreature(key, select) {
  const c = creatures.value.find((x) => x.key === key);
  if (select) select.value = '';                    // the list says "Add a creature…" again
  if (!c) return;
  names.value = { ...names.value, [c.key]: c.name };
  addChild(c.kind === 'npc' ? 'npc' : 'monster', c.name, { creatures: [c.key] });
}
const nameOf = (key) => names.value[key] ?? key.split('/').pop();
async function learnNames() {                      // the names of the creatures an adventure already has
  const keys = new Set();
  const walk = (nodes) => nodes.forEach((n) => { (n.creatures ?? []).forEach((k) => keys.add(k)); walk(n.sections ?? []); });
  walk(doc.value?.chapters ?? []);
  if (!keys.size) return;
  try {
    const shown = (await apiGet(`/gm/adventures/${encodeURIComponent(doc.value.id)}`, props.token)).creatures ?? {};
    names.value = { ...names.value, ...Object.fromEntries(Object.entries(shown).map(([k, v]) => [k, v.name])) };
  } catch {
    /* the keys are shown */
  }
}
function dropCreature(key) {
  node.value.creatures = (node.value.creatures ?? []).filter((k) => k !== key);
}
</script>

<template>
  <section class="adv" data-test="hb-adventure">
    <p v-if="loadError" class="error">{{ loadError }}</p>

    <div class="adv-top panel">
      <label v-if="list.length">Adventure
        <select :value="chosen" aria-label="Adventure" data-test="adv-pick" @change="open($event.target.value)">
          <option v-for="a in list" :key="a.id" :value="a.id">{{ a.title }}</option>
        </select>
      </label>
      <form v-if="naming || !list.length" class="adv-new" @submit.prevent="create">
        <input v-model="nameDraft" maxlength="80" placeholder="Name of the adventure" aria-label="Name of the adventure" data-test="adv-name-new" />
        <button type="submit" class="pill" :disabled="!nameDraft.trim()" data-test="adv-create">Create</button>
        <button v-if="list.length" type="button" class="pill" @click="naming = false">Cancel</button>
      </form>
      <button v-else type="button" class="pill" data-test="adv-new" @click="naming = true">+ New adventure</button>
      <template v-if="doc">
        <label class="adv-title">Title <input v-model="doc.title" maxlength="80" aria-label="Title" data-test="adv-title" /></label>
        <button v-if="confirming !== 'delete-adventure'" type="button" class="pill danger" data-test="adv-delete" @click="confirming = 'delete-adventure'">Delete adventure</button>
        <span v-else class="confirm" data-test="adv-delete-confirm">Delete “{{ doc.title }}” and its pictures?
          <button type="button" class="pill danger" data-test="adv-delete-yes" @click="removeAdventure">Yes, delete</button>
          <button type="button" class="pill" @click="confirming = ''">Cancel</button></span>
      </template>
      <span v-if="status" class="status" :class="{ error: failed }" data-test="adv-status">{{ status }}</span>
    </div>

    <p v-if="!doc && !list.length" class="muted" data-test="adv-empty">Start with a name: you can then add chapters, and sections inside them. It will show in the Adventures tab.</p>

    <div v-if="doc" class="adv-layout">
      <nav class="adv-outline panel" aria-label="Outline">
        <button v-for="r in rows" :key="r.node.id" type="button" class="orow" :class="{ active: selectedId === r.node.id, chapter: r.depth === 0 }" :style="{ paddingLeft: `${8 + r.depth * 16}px` }" data-test="adv-row" @click="selectedId = r.node.id; confirming = ''">
          <span v-if="r.node.number" class="num">{{ r.node.number }}.</span>{{ r.node.title || '(no name)' }}
        </button>
        <button type="button" class="pill add" data-test="add-chapter" @click="addChapter">+ Chapter</button>
      </nav>

      <article v-if="node" class="adv-edit panel" data-test="adv-edit">
        <div class="adv-bar">
          <button type="button" class="pill" data-test="add-section" @click="addChild()">+ Section inside</button>
          <button type="button" class="pill" data-test="move-up" :disabled="here.index === 0" title="Move up" @click="move(-1)">↑</button>
          <button type="button" class="pill" data-test="move-down" :disabled="here.index === here.siblings.length - 1" title="Move down" @click="move(1)">↓</button>
          <button v-if="confirming !== 'delete-node'" type="button" class="pill danger" data-test="delete-node" @click="askRemove">Delete</button>
          <span v-else class="confirm" data-test="delete-node-confirm">Delete this part, with what is inside it?
            <button type="button" class="pill danger" data-test="delete-node-yes" @click="removeNode">Yes</button>
            <button type="button" class="pill" @click="confirming = ''">Cancel</button></span>
        </div>

        <label>Name <input v-model="node.title" maxlength="200" aria-label="Name" data-test="node-title" /></label>
        <div class="adv-row2">
          <label v-if="!isChapter">Kind
            <select v-model="node.kind" aria-label="Kind" data-test="node-kind">
              <option v-for="k in KINDS" :key="k.id" :value="k.id">{{ k.label }}</option>
            </select>
          </label>
          <label v-if="node.kind === 'location'">Number <input v-model="node.number" maxlength="20" class="short" aria-label="Number" data-test="node-number" /></label>
        </div>
        <label>Text <textarea v-model="node.body" rows="9" aria-label="Text" data-test="node-body" placeholder="What the players find, what the GM needs to know…"></textarea></label>

        <div class="adv-pics">
          <p class="adv-h">Pictures</p>
          <div v-for="p in node.images ?? []" :key="p" class="pic-row" data-test="node-picture">
            <AdventurePicture :token="token" :adventure="doc.id" :file="p" :alt="node.title" />
            <button type="button" class="pill" data-test="picture-remove" @click="dropPicture(p)">Remove</button>
          </div>
          <button type="button" class="pill" data-test="picture-add" @click="fileInput.click()">+ Picture</button>
          <input ref="fileInput" type="file" accept="image/png,image/jpeg,image/webp" hidden data-test="picture-file" @change="upload" />
        </div>

        <div class="adv-creatures">
          <p class="adv-h">Creatures</p>
          <p v-if="(node.creatures ?? []).length" class="chips">
            <span v-for="k in node.creatures" :key="k" class="chip creature" data-test="node-creature">{{ nameOf(k) }} <button type="button" class="x" :aria-label="`Remove ${nameOf(k)}`" @click="dropCreature(k)">✕</button></span>
          </p>
          <select aria-label="Add a creature" data-test="creature-pick" :disabled="!creatures.length" @change="addCreature($event.target.value, $event.target)">
            <option value="">{{ creatures.length ? 'Add an NPC or a monster from the Reference…' : 'No creatures to choose from' }}</option>
            <optgroup v-for="g in byKind" :key="g.id" :label="g.label">
              <option v-for="c in g.list" :key="c.key" :value="c.key">{{ c.sub ? c.name + ' · ' + c.sub : c.name }}</option>
            </optgroup>
          </select>
        </div>
      </article>
      <p v-else class="muted adv-hint" data-test="adv-hint">Add a chapter to begin.</p>
    </div>
  </section>
</template>

<style scoped>
.adv { display: flex; flex-direction: column; gap: 10px; }
.adv-top { display: flex; flex-wrap: wrap; align-items: center; gap: 10px 14px; padding: 8px 12px; }
.adv-top select, .adv input, .adv textarea { font: inherit; color: var(--ink); padding: 5px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; }
.adv textarea { width: 100%; box-sizing: border-box; resize: vertical; }
.adv label { display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: var(--muted); }
.adv-edit label { flex-direction: column; align-items: stretch; gap: 4px; }
.adv-edit label input:not(.short) { width: 100%; box-sizing: border-box; }
.adv-row2 { display: flex; gap: 14px; flex-wrap: wrap; }
.adv-new { display: flex; gap: 8px; align-items: center; }
.adv-title input { min-width: 220px; }
.adv-layout { display: grid; grid-template-columns: 1fr; gap: 12px; align-items: start; }
@media (min-width: 820px) { .adv-layout { grid-template-columns: minmax(220px, 300px) 1fr; } }
.adv-outline { display: flex; flex-direction: column; padding: 6px; gap: 2px; }
.orow { text-align: left; background: none; border: 0; border-left: 3px solid transparent; padding: 5px 8px; font: inherit; font-size: 0.9rem; color: var(--ink); cursor: pointer; }
.orow.chapter { font-weight: 700; }
.orow:hover { background: var(--parchment); }
.orow.active { background: #e8f0fe; border-left-color: var(--green-dark); }
.num { margin-right: 4px; color: var(--muted); }
.add { align-self: flex-start; margin-top: 6px; }
.adv-edit { display: flex; flex-direction: column; gap: 10px; padding: 12px; }
.adv-bar { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
.adv-h { margin: 0; font-size: 0.8rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--brown); }
.pic-row { display: flex; align-items: flex-start; gap: 10px; margin: 6px 0; }
.chips { display: flex; flex-wrap: wrap; gap: 6px; margin: 4px 0; }
.chip.creature { display: inline-flex; gap: 6px; align-items: center; padding: 2px 10px; border: 1px solid var(--green-dark); border-radius: 14px; font-size: 0.85rem; }
.x { background: none; border: 0; cursor: pointer; color: var(--muted); padding: 0; }
.adv-creatures select { width: 100%; box-sizing: border-box; }
.pill { padding: 4px 14px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.85rem; font-weight: 600; cursor: pointer; white-space: nowrap; }
.pill:hover:not(:disabled) { background: var(--parchment); }
.pill:disabled { opacity: 0.4; cursor: default; }
.pill.danger { border-color: var(--red); color: var(--red); }
.confirm { display: inline-flex; gap: 8px; align-items: center; flex-wrap: wrap; font-size: 0.85rem; }
.status { font-size: 0.8rem; color: var(--muted); margin-left: auto; }
.status.error { color: var(--red); }
</style>
