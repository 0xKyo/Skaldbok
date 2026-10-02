<script setup>
// The adventures the GM has loaded (a pack with an adventure.yaml): a dropdown to pick one, its outline on the left (chapters, sections,
// locations) and the text of what is picked on the right. A chapter shows its text and the list of what is in it; anything else shows its
// text and, below, everything inside it (the NPCs, sidebars, tables...) in the order of the book.
import { computed, inject, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import DataTable from './DataTable.vue';
import AdventurePicture from './AdventurePicture.vue';
import LinkedText from './LinkedText.vue';
import PinButton from './PinButton.vue';

const props = defineProps({
  token: { type: String, required: true },
});

const OUTLINE = new Set(['chapter', 'section', 'location']);       // what the outline lists; the rest is read inside its parent
const KIND_LABEL = { npc: 'NPC', monster: 'Monster', sidebar: 'Sidebar', map: 'Map', table: 'Table', text: '', location: 'Location', section: '', chapter: 'Chapter' };

const adventures = ref([]);
const chosen = ref('');
const adventure = ref(null);          // the one open: {id, name, title, chapters, tables, creatures}
const selectedId = ref('');
const expanded = ref(new Set());
const contentOpen = ref(false);       // on a phone the outline and the text take turns
const loadError = ref('');
const loading = ref(false);
const nav = inject('nav', null);      // back and forward between what was looked at (App provides it)
let pendingNav = null;                // a step being put back that has to wait for its adventure to load

onMounted(async () => {
  pendingNav = nav?.restoring.value ? nav.slice('adv') : null;      // opened by going back to this tab
  try {
    adventures.value = (await apiGet('/gm/adventures', props.token)).adventures ?? [];
    const wanted = pendingNav?.adventure && adventures.value.some((a) => a.id === pendingNav.adventure) ? pendingNav.adventure : adventures.value[0]?.id;
    if (wanted) chosen.value = wanted;
  } catch (e) {
    loadError.value = e instanceof ApiError ? e.message : 'Could not load the adventures.';
  }
});

watch(chosen, async (id) => {
  adventure.value = null;
  selectedId.value = '';
  contentOpen.value = false;
  expanded.value = new Set();
  if (!id) return;
  loading.value = true;
  try {
    adventure.value = await apiGet(`/gm/adventures/${encodeURIComponent(id)}`, props.token);
    notes.value = { ...(adventure.value.notes ?? {}) };
    loadError.value = '';
    const first = adventure.value.chapters?.[0];
    if (first) selectedId.value = first.id;
    if (pendingNav && pendingNav.adventure === id) {
      const step = pendingNav;
      pendingNav = null;
      await putBackPage(step);
    }
  } catch (e) {
    loadError.value = e instanceof ApiError ? e.message : 'Could not open that adventure.';
  } finally {
    loading.value = false;
  }
});

// every node by id, with its parent, so the outline can say where you are
const index = computed(() => {
  const map = new Map();
  const walk = (nodes, parent) => {
    for (const n of nodes ?? []) {
      map.set(n.id, { node: n, parent });
      walk(n.sections, n);
    }
  };
  walk(adventure.value?.chapters, null);
  return map;
});

const selected = computed(() => index.value.get(selectedId.value)?.node ?? null);
const trail = computed(() => {
  const out = [];
  let at = index.value.get(selectedId.value);
  while (at) {
    out.unshift(at.node);
    at = at.parent ? index.value.get(at.parent.id) : null;
  }
  return out;
});

const outlineKids = (n) => (n.sections ?? []).filter((k) => OUTLINE.has(k.kind));
const inlineKids = (n) => (n.sections ?? []).filter((k) => !OUTLINE.has(k.kind));

function pick(n, manual = false) {
  if (manual) jumps.value = [];
  selectedId.value = n.id;
  contentOpen.value = true;
  if (outlineKids(n).length) expanded.value = new Set([...expanded.value, n.id]);
}

function toggle(n) {
  const next = new Set(expanded.value);
  if (next.has(n.id)) next.delete(n.id);
  else next.add(n.id);
  expanded.value = next;
}

// The visible rows of the outline, flattened with their depth
const rows = computed(() => {
  const out = [];
  const walk = (nodes, depth) => {
    for (const n of nodes ?? []) {
      const kids = outlineKids(n);
      out.push({ node: n, depth, kids: kids.length });
      if (kids.length && expanded.value.has(n.id)) walk(kids, depth + 1);
    }
  };
  walk(adventure.value?.chapters, 0);
  return out;
});

// A text as blocks: paragraphs, and lists for the lines that begin with ✦ (a bold label before the first colon)
function blocks(text) {
  const out = [];
  for (const line of String(text ?? '').split('\n')) {
    const t = line.trim();
    if (!t) continue;
    if (t.startsWith('✦')) {
      const item = t.slice(1).trim();
      const at = item.indexOf(': ');
      const entry = at > 0 && at < 40 ? { label: item.slice(0, at), text: item.slice(at + 2) } : { label: '', text: item };
      const last = out[out.length - 1];
      if (last?.list) last.list.push(entry);
      else out.push({ list: [entry] });
    } else {
      out.push({ p: t });
    }
  }
  return out;
}

const creaturesOf = (n) => (n.creatures ?? []).map((k) => ({ key: k, name: adventure.value?.creatures?.[k]?.name ?? k.split('/').pop() }));
const tableOf = (n) => (n.table ? adventure.value?.tables?.[n.table] ?? null : null);
// the pictures of a node (maps, portraits, illustrations), in the order the pack gives them
const picturesOf = (n) => n?.images ?? [];
const kindLabel = (n) => KIND_LABEL[n.kind] ?? n.kind;

// every word the adventure says leads to a page (a node's `keywords`), longest first so "Crypt of Um-Durman" wins over "Um-Durman"
const matcher = computed(() => {
  const map = new Map();
  for (const { node } of index.value.values())
    for (const word of node.keywords ?? []) if (!map.has(word)) map.set(word, node.id);
  if (!map.size) return null;
  const words = [...map.keys()].sort((a, b) => b.length - a.length).map((w) => w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
  return { map, regex: new RegExp(`(?<![\\p{L}\\p{N}_-])(?:${words.join('|')})(?![\\p{L}\\p{N}_-])`, 'gu') };
});

// where you were before following a link, so there is a way back
const jumps = ref([]);
const here = () => (view.value === 'npcs' ? { view: 'npcs', id: npcId.value } : { view: 'story', id: selectedId.value });

// ---- the NPCs tab: every NPC of the adventure in one place, with its stat block from Core and where in the book it is ----
const view = ref('story');            // 'story' | 'npcs'
const withMonsters = ref(false);
const npcQuery = ref('');
const npcId = ref('');
const npcOpen = ref(false);           // on a phone the list and the NPC take turns
const notes = ref({});                // the GM's own notes, by node id (kept on the server, never in the pack)
const noteText = ref('');
const noteStatus = ref('');           // '', 'Saving…', 'Saved', or what went wrong
let noteTimer = null;
let noteNode = '';                    // the node the text being edited belongs to
const creatureDetail = ref({});       // key -> the creature as the Reference has it, or null while it loads / when there is none

// the chain of nodes from the chapter down to a node (the node included)
function chainOf(id) {
  const out = [];
  let at = index.value.get(id);
  while (at) {
    out.unshift(at.node);
    at = at.parent ? index.value.get(at.parent.id) : null;
  }
  return out;
}

const npcs = computed(() => {
  const out = [];
  for (const { node } of index.value.values()) {
    if (node.kind !== 'npc' && !(withMonsters.value && node.kind === 'monster')) continue;
    const chain = chainOf(node.id);
    const place = chain.slice(0, -1).filter((n) => n.kind !== 'chapter' && n.kind !== 'npc' && n.kind !== 'monster');
    out.push({ node, chapter: chain[0], place: place.map((n) => (n.number ? `${n.number}. ${n.title}` : n.title)).join(' › '), home: [...chain].reverse().find((n) => OUTLINE.has(n.kind) && n.id !== node.id) });
  }
  return out;
});

const npcGroups = computed(() => {
  const q = npcQuery.value.trim().toLowerCase();
  const groups = [];
  for (const row of npcs.value) {
    if (q && !row.node.title.toLowerCase().includes(q)) continue;
    let g = groups.find((x) => x.chapter.id === row.chapter.id);
    if (!g) groups.push((g = { chapter: row.chapter, rows: [] }));
    g.rows.push(row);
  }
  return groups;
});

const npcSelected = computed(() => npcs.value.find((r) => r.node.id === npcId.value) ?? null);

async function pickNpc(row, manual = false) {
  if (manual) jumps.value = [];
  await flushNote();
  npcId.value = row.node.id;
  noteNode = row.node.id;
  noteText.value = notes.value[row.node.id] ?? '';
  noteStatus.value = '';
  npcOpen.value = true;
  for (const key of row.node.creatures ?? []) {
    if (key in creatureDetail.value) continue;
    creatureDetail.value = { ...creatureDetail.value, [key]: null };
    try {
      creatureDetail.value = { ...creatureDetail.value, [key]: await apiGet(`/gm/creatures/${encodeURIComponent(key)}`, props.token) };
    } catch {
      creatureDetail.value = { ...creatureDetail.value, [key]: null };
    }
  }
}

// the note is saved a moment after the last key, when the box loses focus, and when another NPC (or tab) is opened
function editNote() {
  clearTimeout(noteTimer);
  noteStatus.value = '';
  noteTimer = setTimeout(flushNote, 800);
}

async function flushNote() {
  clearTimeout(noteTimer);
  if (!noteNode || !chosen.value || (notes.value[noteNode] ?? '') === noteText.value) return;
  const node = noteNode, text = noteText.value;
  noteStatus.value = 'Saving…';
  try {
    await apiSend('PUT', `/gm/adventures/${encodeURIComponent(chosen.value)}/notes`, props.token, { node, text });
    notes.value = { ...notes.value, [node]: text };
    if (!text.trim()) delete notes.value[node];
    noteStatus.value = node === noteNode ? 'Saved' : '';
  } catch (e) {
    noteStatus.value = e instanceof ApiError ? e.message : 'Could not save the note.';
  }
}

onBeforeUnmount(flushNote);
watch(view, flushNote);

// an NPC (or monster) met in the story opens its own page in the NPCs tab
const isPerson = (n) => n.kind === 'npc' || n.kind === 'monster';

async function openNpc(node) {
  if (node.kind === 'monster') withMonsters.value = true;
  npcQuery.value = '';
  view.value = 'npcs';
  const row = npcs.value.find((r) => r.node.id === node.id);
  if (row) await pickNpc(row);
}

// a link of the text: a person goes to the NPCs tab, anything else to the page it belongs to (opened in the outline)
async function goTo(id) {
  const target = index.value.get(id)?.node;
  if (!target) return;
  jumps.value = [...jumps.value, here()];
  if (isPerson(target)) {
    await openNpc(target);
    return;
  }
  const chain = chainOf(id);
  const page = [...chain].reverse().find((n) => OUTLINE.has(n.kind)) ?? chain[0];
  view.value = 'story';
  expanded.value = new Set([...expanded.value, ...chain.filter((n) => n.id !== page.id || outlineKids(n).length).map((n) => n.id)]);
  selectedId.value = page.id;
  contentOpen.value = true;
}

async function goBack() {
  const back = jumps.value[jumps.value.length - 1];
  if (!back) return;
  jumps.value = jumps.value.slice(0, -1);
  if (back.view === 'npcs') {
    view.value = 'npcs';
    const row = npcs.value.find((r) => r.node.id === back.id);
    if (row) await pickNpc(row);
  } else {
    view.value = 'story';
    selectedId.value = back.id;
    contentOpen.value = true;
  }
}

function showInStory(row) {
  view.value = 'story';
  if (row.home) pick(row.home);
}

watch(chosen, () => {
  view.value = 'story';
  npcId.value = '';
  npcOpen.value = false;
});
// ---- what the 📌 button puts on the master's board: the page open (with what is inside it), or the NPC open with its stat block ----
function pinPage() {
  const n = selected.value;
  if (!n) return null;
  const kids = n.kind === 'chapter' ? inlineKids(n) : n.sections ?? [];
  const tables = [n, ...kids].map(tableOf).filter(Boolean);
  const creatures = [n, ...kids].flatMap((k) => creaturesOf(k).map((c) => c.name));
  return {
    type: 'adventure',
    key: `adventure:${chosen.value}:${n.id}`,
    title: n.number ? `${n.number}. ${n.title}` : n.title,
    data: { body: n.body ?? '', sections: kids.map((k) => ({ title: k.title, body: k.body ?? '' })), tables, creatures },
    w: 380,
    h: 320,
  };
}

function pinNpc() {
  const row = npcSelected.value;
  if (!row) return null;
  const c = creatureDetail.value[row.node.creatures?.[0]] ?? {};
  return {
    type: 'creature',
    key: `npc:${chosen.value}:${row.node.id}`,
    title: row.node.title,
    data: { kind: row.node.kind, category: row.place, intro: row.node.body ?? '', blocks: c.blocks ?? [], attacks: c.attacks ?? [], abilities: c.abilities ?? [], description: '' },
    w: 360,
    h: 340,
  };
}

// ---- back and forward: the adventure, the tab, the page and the NPC are one step of what was looked at ----
const navState = computed(() => (adventure.value ? { adventure: chosen.value, view: view.value, story: selectedId.value, npc: npcId.value, monsters: withMonsters.value } : null));
const expandTo = (id) => { expanded.value = new Set([...expanded.value, ...chainOf(id).map((n) => n.id)]); };

async function putBackPage(s) {
  withMonsters.value = !!s.monsters;
  view.value = s.view ?? 'story';
  if (s.story && index.value.has(s.story)) {
    expandTo(s.story);
    selectedId.value = s.story;
    contentOpen.value = true;
  }
  const row = s.npc ? npcs.value.find((r) => r.node.id === s.npc) : null;
  if (row) await pickNpc(row);
  else npcId.value = '';
}

if (nav) {
  watch(navState, (s) => { if (s) nav.record('adv', s); }, { immediate: true });
  watch(() => nav.tick.value, async () => {
    const s = nav.slice('adv');
    if (!s?.adventure) return;
    if (s.adventure !== chosen.value) {
      pendingNav = s;
      chosen.value = s.adventure;
    } else {
      await putBackPage(s);
    }
  });
}
</script>

<template>
  <section class="adventures" data-test="adventures">
    <p v-if="loadError" class="error" data-test="load-error">{{ loadError }}</p>

    <p v-if="!adventures.length && !loadError" class="muted" data-test="none">There is no adventure loaded. An adventure is a pack with an <code>adventure.yaml</code>.</p>

    <label v-if="adventures.length" class="picker">Adventure
      <select v-model="chosen" aria-label="Adventure" data-test="adventure-pick">
        <option v-for="a in adventures" :key="a.id" :value="a.id">{{ a.name }}</option>
      </select>
    </label>

    <p v-if="loading" class="muted">Opening…</p>

    <nav v-if="adventure" class="views" aria-label="What to read">
      <button type="button" class="vtab" :class="{ active: view === 'story' }" data-test="tab-story" @click="view = 'story'">Story</button>
      <button type="button" class="vtab" :class="{ active: view === 'npcs' }" data-test="tab-npcs" @click="view = 'npcs'">NPCs ({{ npcs.length }})</button>
    </nav>

    <div v-if="adventure && view === 'npcs'" class="layout" :class="{ 'content-open': npcOpen }" data-test="npcs">
      <aside class="outline panel" aria-label="NPCs">
        <input v-model="npcQuery" type="search" class="npc-search" placeholder="Search the NPCs…" aria-label="Search the NPCs" data-test="npc-search" />
        <label class="check"><input v-model="withMonsters" type="checkbox" data-test="npc-monsters" /> Monsters too</label>
        <p v-if="!npcGroups.length" class="muted">No NPC matches.</p>
        <template v-for="g in npcGroups" :key="g.chapter.id">
          <h3 class="ngroup">{{ g.chapter.title }}</h3>
          <button v-for="r in g.rows" :key="r.node.id" type="button" class="nrow" :class="{ active: npcId === r.node.id }" data-test="npc-row" @click="pickNpc(r, true)">
            <span class="nname">{{ r.node.title }}<span v-if="notes[r.node.id]" class="has-note" title="You have notes on it" data-test="has-note"> ✎</span></span><span v-if="r.place" class="nplace">{{ r.place }}</span>
          </button>
        </template>
      </aside>

      <article v-if="npcSelected" class="page panel" data-test="npc-page">
        <button type="button" class="back link" data-test="npc-back" @click="npcOpen = false">← NPCs</button>
        <button v-if="jumps.length" type="button" class="link jump-back" data-test="jump-back" @click="goBack">↩ Back</button>
        <h2>{{ npcSelected.node.title }} <span class="chip">{{ kindLabel(npcSelected.node) }}</span> <span v-if="npcSelected.node.page" class="muted pg">p.{{ npcSelected.node.page }}</span> <PinButton :snapshot="pinNpc" small /></h2>
        <AdventurePicture v-for="file in picturesOf(npcSelected.node)" :key="file" :token="token" :adventure="chosen" :file="file" :alt="npcSelected.node.title" />
        <p class="trail muted" data-test="npc-place">{{ npcSelected.chapter.title }}<template v-if="npcSelected.place"> › {{ npcSelected.place }}</template>
          <a v-if="npcSelected.home" href="#" class="story-link" data-test="npc-story" @click.prevent="showInStory(npcSelected)">Open in the story</a></p>
        <template v-for="(b, i) in blocks(npcSelected.node.body)" :key="i">
          <p v-if="b.p"><LinkedText :text="b.p" :matcher="matcher" :owner="npcSelected.node.id" @go="goTo" /></p>
          <ul v-else class="bullets"><li v-for="(it, j) in b.list" :key="j"><b v-if="it.label">{{ it.label }}.</b> <LinkedText :text="it.text" :matcher="matcher" :owner="npcSelected.node.id" @go="goTo" /></li></ul>
        </template>
        <section v-for="key in npcSelected.node.creatures ?? []" :key="key" class="stats" data-test="npc-stats">
          <p v-if="creatureDetail[key] === null" class="muted">…</p>
          <template v-if="creatureDetail[key]">
            <h3>{{ creatureDetail[key].name }}</h3>
            <div v-for="(block, bi) in creatureDetail[key].blocks" :key="bi" class="stat-block">
              <h4 v-if="block.variant">{{ block.variant }}</h4>
              <table class="fields-table"><tbody><tr v-for="f in block.fields" :key="f.label"><th>{{ f.label }}</th><td>{{ f.value }}</td></tr></tbody></table>
            </div>
            <table v-if="creatureDetail[key].attacks.length" class="attacks-table">
              <thead><tr><th>Roll</th><th>Attack</th><th>Effect</th></tr></thead>
              <tbody><tr v-for="(a, ai) in creatureDetail[key].attacks" :key="ai"><td>{{ a.rollText || `${a.rollMin}–${a.rollMax}` }}</td><td>{{ a.name }}</td><td>{{ a.text }}</td></tr></tbody>
            </table>
            <div v-for="a in creatureDetail[key].abilities" :key="a.name" class="ability"><b>{{ a.name }}.</b> {{ a.text }}</div>
          </template>
        </section>
        <section class="notes" data-test="notes">
          <h3>Notes</h3>
          <textarea v-model="noteText" rows="6" maxlength="20000" placeholder="Your notes on this NPC: what the players learned, what you changed, what comes next…" aria-label="Notes" data-test="note" @input="editNote" @blur="flushNote"></textarea>
          <p v-if="noteStatus" class="muted note-status" data-test="note-status">{{ noteStatus }}</p>
        </section>
      </article>
      <p v-else class="muted hint">Pick an NPC.</p>
    </div>

    <div v-if="adventure && view === 'story'" class="layout" :class="{ 'content-open': contentOpen }">
      <nav class="outline panel" aria-label="Outline" data-test="outline">
        <div v-for="r in rows" :key="r.node.id" class="orow" :style="{ paddingLeft: `${r.depth * 14}px` }">
          <button v-if="r.kids" type="button" class="twist" :aria-label="expanded.has(r.node.id) ? 'Collapse' : 'Expand'" :aria-expanded="expanded.has(r.node.id)" @click="toggle(r.node)">{{ expanded.has(r.node.id) ? '▾' : '▸' }}</button>
          <span v-else class="twist"></span>
          <button type="button" class="olabel" :class="{ active: selectedId === r.node.id, chapter: r.node.kind === 'chapter' }" data-test="outline-item" @click="pick(r.node, true)">
            <span v-if="r.node.number" class="num">{{ r.node.number }}.</span>{{ r.node.title }}
          </button>
        </div>
      </nav>

      <article v-if="selected" class="page panel" data-test="page">
        <button type="button" class="back link" data-test="back" @click="contentOpen = false">← Outline</button>
        <button v-if="jumps.length" type="button" class="link jump-back" data-test="jump-back" @click="goBack">↩ Back</button>
        <p v-if="trail.length > 1" class="trail muted" data-test="trail">
          <template v-for="(t, i) in trail.slice(0, -1)" :key="t.id"><a href="#" @click.prevent="pick(t)">{{ t.title }}</a><span v-if="i < trail.length - 2"> › </span></template>
        </p>
        <h2>{{ selected.number ? `${selected.number}. ` : '' }}{{ selected.title }} <span v-if="kindLabel(selected)" class="chip">{{ kindLabel(selected) }}</span> <span v-if="selected.page" class="muted pg">p.{{ selected.page }}</span> <PinButton :snapshot="pinPage" small /></h2>

        <AdventurePicture v-for="file in picturesOf(selected)" :key="file" :token="token" :adventure="chosen" :file="file" :alt="selected.title" />
        <template v-for="(b, i) in blocks(selected.body)" :key="i">
          <p v-if="b.p"><LinkedText :text="b.p" :matcher="matcher" :owner="selected.id" @go="goTo" /></p>
          <ul v-else class="bullets"><li v-for="(it, j) in b.list" :key="j"><b v-if="it.label">{{ it.label }}.</b> <LinkedText :text="it.text" :matcher="matcher" :owner="selected.id" @go="goTo" /></li></ul>
        </template>

        <p v-if="creaturesOf(selected).length" class="creatures" data-test="creatures">
          <span v-for="c in creaturesOf(selected)" :key="c.key" class="chip creature">{{ c.name }}</span>
        </p>
        <DataTable v-if="tableOf(selected)" :table="tableOf(selected)" />

        <!-- a chapter lists what is in it; the rest shows what is inside, in the order of the book -->
        <ul v-if="selected.kind === 'chapter' && outlineKids(selected).length" class="contents" data-test="contents">
          <li v-for="k in outlineKids(selected)" :key="k.id"><a href="#" @click.prevent="pick(k)">{{ k.title }}</a></li>
        </ul>

        <template v-for="k in (selected.kind === 'chapter' ? inlineKids(selected) : selected.sections ?? [])" :key="k.id">
          <section class="inner" data-test="inner">
            <h3>{{ k.number ? `${k.number}. ` : '' }}<a v-if="isPerson(k)" href="#" class="npc-link" data-test="story-npc" @click.prevent="openNpc(k)">{{ k.title }}</a><template v-else>{{ k.title }}</template> <span v-if="kindLabel(k)" class="chip">{{ kindLabel(k) }}</span></h3>
            <AdventurePicture v-for="file in picturesOf(k)" :key="file" :token="token" :adventure="chosen" :file="file" :alt="k.title" />
            <template v-for="(b, i) in blocks(k.body)" :key="i">
              <p v-if="b.p"><LinkedText :text="b.p" :matcher="matcher" :owner="k.id" @go="goTo" /></p>
              <ul v-else class="bullets"><li v-for="(it, j) in b.list" :key="j"><b v-if="it.label">{{ it.label }}.</b> <LinkedText :text="it.text" :matcher="matcher" :owner="k.id" @go="goTo" /></li></ul>
            </template>
            <p v-if="creaturesOf(k).length" class="creatures"><span v-for="c in creaturesOf(k)" :key="c.key" class="chip creature">{{ c.name }}</span></p>
            <DataTable v-if="tableOf(k)" :table="tableOf(k)" />
            <section v-for="g in k.sections ?? []" :key="g.id" class="inner deeper">
              <h4>{{ g.number ? `${g.number}. ` : '' }}<a v-if="isPerson(g)" href="#" class="npc-link" data-test="story-npc" @click.prevent="openNpc(g)">{{ g.title }}</a><template v-else>{{ g.title }}</template> <span v-if="kindLabel(g)" class="chip">{{ kindLabel(g) }}</span></h4>
              <AdventurePicture v-for="file in picturesOf(g)" :key="file" :token="token" :adventure="chosen" :file="file" :alt="g.title" />
              <template v-for="(b, i) in blocks(g.body)" :key="i">
                <p v-if="b.p"><LinkedText :text="b.p" :matcher="matcher" :owner="g.id" @go="goTo" /></p>
                <ul v-else class="bullets"><li v-for="(it, j) in b.list" :key="j"><b v-if="it.label">{{ it.label }}.</b> <LinkedText :text="it.text" :matcher="matcher" :owner="g.id" @go="goTo" /></li></ul>
              </template>
              <p v-if="creaturesOf(g).length" class="creatures"><span v-for="c in creaturesOf(g)" :key="c.key" class="chip creature">{{ c.name }}</span></p>
              <DataTable v-if="tableOf(g)" :table="tableOf(g)" />
            </section>
          </section>
        </template>
      </article>
    </div>
  </section>
</template>

<style scoped>
.picker { display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: var(--muted); margin-bottom: 10px; }
.picker select { font: inherit; color: var(--ink); padding: 4px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; }
.layout { display: grid; grid-template-columns: 1fr; gap: 14px; align-items: start; }
@media (min-width: 820px) { .layout { grid-template-columns: minmax(230px, 320px) 1fr; } .back { display: none; } .outline { position: sticky; top: 8px; max-height: calc(100vh - 120px); overflow-y: auto; } }
@media (max-width: 819px) { .layout:not(.content-open) .page { display: none; } .layout.content-open .outline { display: none; } }
.outline { padding: 8px; }
.views { display: flex; gap: 6px; margin-bottom: 10px; }
.vtab { padding: 4px 16px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.85rem; font-weight: 600; cursor: pointer; }
.vtab:hover { background: var(--parchment); }
.vtab.active { background: var(--green-dark); color: #f3ead2; }
.npc-search { width: 100%; box-sizing: border-box; font: inherit; padding: 6px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; margin-bottom: 6px; }
.check { display: flex; align-items: center; gap: 6px; font-size: 0.8rem; color: var(--muted); margin-bottom: 4px; }
.ngroup { margin: 10px 0 2px; font-size: 0.8rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--brown); }
.nrow { display: flex; flex-direction: column; align-items: flex-start; width: 100%; border: 0; border-bottom: 1px solid var(--line); background: none; font: inherit; color: var(--ink); text-align: left; padding: 5px 8px; cursor: pointer; }
.nrow:hover { background: var(--parchment); }
.nrow.active { background: var(--green-dark); color: #f3ead2; }
.nname { font-weight: 600; font-size: 0.9rem; }
.nplace { font-size: 0.75rem; opacity: 0.7; }
.jump-back { display: block; margin: 0 0 6px; border: 0; background: none; color: var(--brown); font: inherit; font-size: 0.85rem; cursor: pointer; padding: 0; }
.npc-link { color: var(--green-dark); text-decoration: underline dotted; }
.story-link { margin-left: 10px; color: var(--green-dark); }
.stats { margin-top: 14px; padding-top: 8px; border-top: 1px solid var(--line); }
.stats h3 { border-top: 0; margin-top: 0; }
.fields-table, .attacks-table { border-collapse: collapse; font-size: 0.85rem; margin: 4px 0 10px; }
.fields-table th { text-align: left; padding: 2px 12px 2px 0; color: var(--muted); font-weight: 600; }
.attacks-table th, .attacks-table td { text-align: left; padding: 3px 10px 3px 0; vertical-align: top; }
.attacks-table th { color: var(--muted); }
.ability { font-size: 0.85rem; margin-bottom: 4px; }
.hint { padding: 1rem; }
.notes { margin-top: 16px; padding-top: 8px; border-top: 1px solid var(--line); }
.notes h3 { border-top: 0; margin-top: 0; }
.notes textarea { width: 100%; box-sizing: border-box; font: inherit; color: var(--ink); padding: 8px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; resize: vertical; }
.note-status { margin: 4px 0 0; font-size: 0.8rem; }
.has-note { color: var(--brown); }
.orow { display: flex; align-items: baseline; }
.twist { width: 1.4rem; flex: none; border: 0; background: none; color: var(--brown); font: inherit; cursor: pointer; padding: 0; }
.olabel { flex: 1; text-align: left; border: 0; border-radius: 6px; background: none; font: inherit; font-size: 0.9rem; color: var(--ink); padding: 3px 6px; cursor: pointer; }
.olabel.chapter { font-weight: 700; color: var(--green-dark); }
.olabel:hover { background: var(--parchment); }
.olabel.active { background: var(--green-dark); color: #f3ead2; }
.num { color: var(--brown); margin-right: 4px; }
.olabel.active .num { color: #f3ead2; }
.page { padding: 12px 16px; }
.page h2 { margin: 0 0 8px; font-size: 1.25rem; color: var(--green-dark); }
.page h3 { margin: 18px 0 6px; font-size: 1rem; color: var(--green-dark); border-top: 1px solid var(--line); padding-top: 10px; }
.page h4 { margin: 12px 0 4px; font-size: 0.9rem; color: var(--brown); }
.pg { font-size: 0.8rem; font-weight: 400; }
.chip { display: inline-block; padding: 1px 8px; border: 1px solid var(--line); border-radius: 12px; font-size: 0.7rem; font-weight: 600; letter-spacing: 0.04em; text-transform: uppercase; color: var(--brown); vertical-align: middle; }
.chip.creature { text-transform: none; font-size: 0.8rem; color: var(--green-dark); border-color: var(--green-dark); margin-right: 6px; }
.trail { margin: 0 0 6px; font-size: 0.8rem; }
.trail a { color: var(--brown); }
.bullets { margin: 6px 0; padding-left: 1.2rem; }
.bullets li { margin-bottom: 4px; }
.contents { columns: 2; margin: 8px 0; padding-left: 1.2rem; }
.contents a { color: var(--green-dark); }
.inner.deeper { margin-left: 12px; padding-left: 10px; border-left: 2px solid var(--line); }
.muted { color: var(--muted); }
.error { color: var(--danger, #c0392b); }
</style>
