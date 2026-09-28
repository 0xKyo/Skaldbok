<script setup>
// The character sheet, laid out like the first page of the printed one (the green page) and arranged for a phone: the same blocks in
// the order a player needs them at the table, and the printed three-column layout on a wide screen.
//
// A player can edit all of it (kin, profession and school aside: those are chosen when the GM creates the character). Changes go to the
// server a moment after they are made, field by field. Breaking a rule is allowed but shows in red, and the GM decides.
import { computed, onBeforeUnmount, ref, watch } from 'vue';
import { ApiError, apiSend } from '../api.js';
import { adopt, clone, diffDoc, issueFor } from '../lib/sheetdoc.js';
import EntryPicker from './EntryPicker.vue';
import Points from './Points.vue';

const SAVE_DELAY_MS = 400;
const RETRY_MS = 3000;

const props = defineProps({
  me: { type: Object, required: true },
  token: { type: String, default: '' },
  patchPath: { type: String, default: '/me' }, // '/me' for player, '/gm/characters/:id' for GM
});
const emit = defineEmits(['updated', 'goto-rules']);

function typeFromKey(key) {
  const seg = key?.split('/')?.[1];
  const map = { skill: 'skills', ability: 'abilities', spell: 'spells', weapon: 'weapons', armor: 'armor', gear: 'gear', kin: 'kin', profession: 'professions' };
  return map[seg] ?? null;
}

function gotoRules(key) {
  const type = typeFromKey(key);
  if (key && type) emit('goto-rules', { key, type });
}

// ------------------------------------------------------------------------------------------------- reading
const skillsShown = computed(() => props.me.skills);
const coreSkills = computed(() => skillsShown.value.filter((s) => s.category === 'core'));
const weaponSkills = computed(() => skillsShown.value.filter((s) => s.category === 'weapon'));
const otherSkills = computed(() => skillsShown.value.filter((s) => s.category !== 'core' && s.category !== 'weapon'));

const conditionOf = (attribute) => props.me.conditions.find((c) => c.attribute === attribute);
const activeConditions = computed(() => props.me.conditions.filter((c) => c.active));
const hasBane = (s) => activeConditions.value.some((c) => c.attribute === s.attribute);
const bonus = (v) => (v ? `+${v}` : '–');
const eq = computed(() => props.me.equipment);
const stat = (item, label) => item?.stats.find((f) => f.label === label)?.value ?? '';
const issues = computed(() => props.me.issues ?? []);
const flagged = (key) => !!issueFor(issues.value, key);

const inventoryLines = computed(() => {
  const count = Math.max(8, eq.value.inventory.length, props.me.derived.encumbrance.limit);
  return Array.from({ length: count }, (_, i) => eq.value.inventory[i] ?? null);
});
const overloaded = computed(() => props.me.derived.encumbrance.carried > props.me.derived.encumbrance.limit);

// ------------------------------------------------------------------------------------------------ editing (always-on; no mode toggle)
const lockedNow = ref(false);
const editing = computed(() => !!props.me.doc && !props.me.locked && !lockedNow.value);
const draft = ref(props.me.doc ? clone(props.me.doc) : null);
const base = ref(props.me.doc ? clone(props.me.doc) : null);
const saveError = ref('');
const dragFrom = ref(null);
const dragOver = ref(null);
let timer = null;
let saving = false;

// adopt server changes without overwriting local edits in progress
watch(
  () => props.me.doc,
  (doc) => {
    if (!doc) return;
    if (draft.value) {
      draft.value = adopt(draft.value, base.value, doc);
    } else {
      draft.value = clone(doc);
    }
    base.value = clone(doc);
  },
);
watch(draft, () => queueSave(), { deep: true });
onBeforeUnmount(() => clearTimeout(timer));

function onDragStart(i, e) { dragFrom.value = i; e.dataTransfer.effectAllowed = 'move'; }
function onDragOver(i, e) { e.preventDefault(); dragOver.value = i; }
function onDragEnd() { dragFrom.value = null; dragOver.value = null; }
function onDrop(i) {
  if (dragFrom.value !== null && dragFrom.value !== i) {
    const items = draft.value.inventory;
    const [moved] = items.splice(dragFrom.value, 1);
    items.splice(i, 0, moved);
  }
  dragFrom.value = null;
  dragOver.value = null;
}

function queueSave(delay = SAVE_DELAY_MS) {
  if (!editing.value) return;
  clearTimeout(timer);
  timer = setTimeout(save, delay);
}

async function save() {
  if (saving || !editing.value || !draft.value) return;
  const sent = clone(draft.value);
  const set = diffDoc(base.value, sent);
  if (!Object.keys(set).length) return;
  saving = true;
  try {
    const fresh = await apiSend('PATCH', props.patchPath, props.token, { set });
    saveError.value = '';
    emit('updated', fresh);
    draft.value = adopt(draft.value, sent, fresh.doc);
    base.value = clone(fresh.doc);
  } catch (e) {
    if (e instanceof ApiError && e.status === 423) {
      lockedNow.value = true;
      saveError.value = e.message;
    } else if (e instanceof ApiError && e.status === 0) {
      saveError.value = 'Offline: your change will be sent when the connection is back.';
      queueSave(RETRY_MS);
    } else {
      saveError.value = e.message; // the server said no: go back to what it has
      draft.value = clone(props.me.doc);
      base.value = clone(props.me.doc);
    }
  } finally {
    saving = false;
  }
  if (editing.value && Object.keys(diffDoc(base.value, draft.value)).length) queueSave();
}

// ---- the small edits
const num = (value, lo, hi) => Math.min(hi, Math.max(lo, Math.trunc(Number(value) || 0)));
const setAttribute = (key, value) => (draft.value.attributes[key] = num(value, 0, 99));
const setCoin = (key, value) => (draft.value.coins[key] = num(value, 0, 99999));

function toggleCondition(name) {
  const list = draft.value.conditions;
  const at = list.indexOf(name);
  if (at === -1) list.push(name);
  else list.splice(at, 1);
}
const isCondition = (name) => draft.value.conditions.includes(name);

// skills: the sheet holds an entry only for a skill that is trained, marked or has a level of its own
const skillEntry = (s, create = false) => {
  let entry = draft.value.skills.find((e) => e.name === s.name);
  if (!entry && create) {
    entry = { name: s.name, key: s.key ?? '', attribute: s.attribute, level: 0, trained: false, marked: false };
    draft.value.skills.push(entry);
  }
  return entry;
};
const tidySkills = () => (draft.value.skills = draft.value.skills.filter((e) => e.trained || e.marked || e.level > 0));
const levelOf = (s) => skillEntry(s)?.level || s.base;
const isTrained = (s) => !!skillEntry(s)?.trained;
const isMarked = (s) => !!skillEntry(s)?.marked;
function setLevel(s, value) {
  const entry = skillEntry(s, true);
  entry.level = num(value, 0, 99);
  if (!entry.trained && entry.level === s.base) entry.level = 0;
  tidySkills();
}
function toggleTrained(s) {
  const entry = skillEntry(s, true);
  entry.trained = !entry.trained;
  entry.level = entry.trained ? Math.min(18, 2 * s.base) : 0;
  tidySkills();
}
function toggleMark(s) {
  const entry = skillEntry(s, true);
  entry.marked = !entry.marked;
  tidySkills();
}

const removeAt = (list, i) => list.splice(i, 1);
const addItem = (list) => list.push({ name: '', count: 1 });
const addRef = (list, entry) => list.push({ key: entry.key, name: entry.name });
const addWeapon = (entry) => draft.value.weapons.push({ key: entry.key, name: entry.name });
const chooseGear = (which, entry) => (draft.value[which] = { key: entry.key, name: entry.name });
const setInt = (target, key, value, lo, hi) => (target[key] = num(value, lo, hi));
</script>

<template>
  <section class="sheet" :class="{ editing }" aria-label="Character sheet">
    <div v-if="me.locked || lockedNow || saveError" class="a-tools">
      <span v-if="me.locked || lockedNow" class="locked-note" data-test="locked">Your GM has locked your sheet.</span>
      <span v-if="saveError" class="error small" data-test="save-error">{{ saveError }}</span>
    </div>

    <div v-if="issues.length" class="a-issues" data-test="issues">
      <div v-for="i in issues" :key="i.key" class="issue" :class="i.status" data-test="issue">
        <b>{{ i.status === 'rejected' ? 'Your GM says no' : 'Rule check' }}</b>
        {{ i.message }}
        <span class="muted small">{{ i.status === 'rejected' ? ' — change it back yourself.' : ' — your GM will look at it.' }}</span>
      </div>
    </div>

    <div class="a-name scroll">
      <template v-if="editing">
        <div class="name-line">
          <input v-model="draft.name" class="edit the-name" maxlength="80" aria-label="Name" data-test="name" />
          <input v-model="draft.nickname" class="edit the-nick" maxlength="80" placeholder="Nickname" aria-label="Nickname" />
        </div>
      </template>
      <template v-else>
        <div class="name-line">
          <div class="the-name" data-test="name">{{ me.name }}</div>
          <div v-if="me.nickname" class="the-nick">"{{ me.nickname }}"</div>
        </div>
      </template>
    </div>

    <div class="a-ident lines">
      <div class="line"><span class="k">Player</span>{{ me.player }}</div>
      <div class="line">
        <span class="k">Kin</span>
        <span class="kin-name">{{ me.kin.name }}</span>
        <span class="muted">·</span>
        <select v-if="editing" v-model="draft.age" class="edit age-select" aria-label="Age">
          <option value="young">Young</option>
          <option value="adult">Adult</option>
          <option value="old">Old</option>
        </select>
        <template v-else>{{ me.age.label }}</template>
      </div>
      <div class="line line-top">
        <span class="k">Appearance</span>
        <textarea v-if="editing" v-model="draft.appearance" class="edit" rows="3" maxlength="1500" aria-label="Appearance"></textarea>
        <span v-else class="line-text">{{ me.about.appearance }}</span>
      </div>
    </div>

    <div class="a-appear lines">
      <div class="line"><span class="k">Profession</span>{{ me.profession.name }}<span v-if="me.school" class="muted">&nbsp;· {{ me.school }}</span></div>
      <div v-if="me.party" class="line"><span class="k">Party</span>{{ me.party }}</div>
      <div class="line line-top">
        <span class="k">Weakness</span>
        <textarea v-if="editing" v-model="draft.weakness" class="edit" rows="1" maxlength="400" aria-label="Weakness"></textarea>
        <span v-else class="line-text">{{ me.about.weakness }}</span>
      </div>
    </div>

    <div class="a-attrs">
      <div class="gems" data-test="conditions">
        <div v-for="a in me.attributes" :key="a.key" class="gem" :class="{ flagged: flagged('attr:' + a.key) }" :title="a.name">
          <span class="abbr">{{ a.key }}</span>
          <span class="ring">
            <input v-if="editing" type="number" class="edit ring-input" min="0" max="99" :value="draft.attributes[a.key]" :aria-label="a.name" data-test="attribute-input" @input="setAttribute(a.key, $event.target.value)" />
            <b v-else data-test="attribute">{{ a.value }}</b>
          </span>
          <span class="base">base chance {{ a.baseChance }}</span>
          <component
            :is="editing ? 'button' : 'span'"
            v-if="conditionOf(a.key)"
            class="cond"
            :class="{ on: editing ? isCondition(conditionOf(a.key).name) : conditionOf(a.key).active }"
            :type="editing ? 'button' : undefined"
            :title="`A bane on ${a.key} and the skills based on it`"
            @click="editing && toggleCondition(conditionOf(a.key).name)"
          >
            <span class="dia"></span>{{ conditionOf(a.key).name }}
          </component>
        </div>
      </div>
    </div>

    <div class="a-points">
    <div class="a-hp">
      <Points
        label="HP"
        :current="editing ? draft.hp : me.hp.current"
        :max="me.hp.max"
        kind="hp"
        :editable="editing"
        :flagged="flagged('hp') || flagged('hpmax')"
        @update:current="(v) => (draft.hp = num(v, 0, 999))"
      />
    </div>
    <div class="a-wp">
      <Points
        label="WP"
        :current="editing ? draft.wp : me.wp.current"
        :max="me.wp.max"
        kind="wp"
        :editable="editing"
        :flagged="flagged('wp') || flagged('wpmax')"
        @update:current="(v) => (draft.wp = num(v, 0, 999))"
      />
    </div>
    </div>

    <div class="a-skills parchment" data-test="skills">
      <span class="banner">Skills</span>
      <div class="skill-cols">
        <template v-for="group in [{ title: 'Skills', list: coreSkills }, { title: 'Weapon skills', list: weaponSkills }, { title: 'Secondary skills', list: otherSkills }]" :key="group.title">
          <div v-if="group.list.length" :class="group.title === 'Skills' ? '' : 'skill-group'">
            <div class="skill-head">{{ group.title }}</div>
            <div v-for="s in group.list" :key="s.name" class="skill" :class="{ untrained: editing ? !isTrained(s) : !s.trained, flagged: flagged('skill:' + s.name) || hasBane(s) }">
              <template v-if="editing">
                <button type="button" class="dia-btn" :aria-pressed="isMarked(s)" :aria-label="`Advancement mark for ${s.name}`" @click="toggleMark(s)"><span class="dia" :class="{ on: isMarked(s) }"></span></button>
                <input type="number" class="edit lvl-input" min="0" max="99" :value="levelOf(s)" :aria-label="`${s.name} level`" data-test="skill-input" @input="setLevel(s, $event.target.value)" />
                <button type="button" class="skill-link" @click="gotoRules(s.key)">{{ s.name }} <span class="attr">({{ s.attribute }})</span></button>
              </template>
              <template v-else>
                <span class="dia" :class="{ on: s.marked }" :title="s.marked ? 'Advancement mark' : ''"></span>
                <span class="lvl" data-test="skill-level">{{ s.level }}</span>
                <button type="button" class="skill-link" @click="gotoRules(s.key)">{{ s.name }} <span class="attr">({{ s.attribute }})</span></button>
              </template>
            </div>
          </div>
        </template>
      </div>
      <p v-if="!skillsShown.length" class="muted">No skills to show.</p>
      <p class="muted small" style="margin: 10px 0 0">A skill is rolled with a D20: at or below its level succeeds, 1 is a dragon, 20 is a demon. Untrained skills use their base chance.</p>
    </div>

    <div class="a-abil block">
      <div class="field-row"><span class="banner">Damage bon. STR</span><span class="field" data-test="damage-str">{{ bonus(me.derived.damageBonus.str) }}</span></div>
      <div class="field-row"><span class="banner">Damage bon. AGL</span><span class="field" data-test="damage-agl">{{ bonus(me.derived.damageBonus.agl) }}</span></div>
      <div class="field-row"><span class="banner">Movement</span><span class="field" data-test="movement">{{ me.derived.movement ?? '?' }}</span></div>
      <span class="banner">Abilities &amp; spells</span>
      <p v-if="!me.abilities.length && !me.spells.length && !editing" class="muted">None yet.</p>
      <template v-if="editing">
        <div v-for="(a, i) in draft.abilities" :key="'a' + i" class="edit-row">
          <button type="button" class="link" @click="gotoRules(a.key)">{{ a.name }}</button>
          <button type="button" class="x" :aria-label="`Remove ${a.name}`" @click="removeAt(draft.abilities, i)">×</button>
        </div>
        <EntryPicker :token="token" type="abilities" label="Add an ability…" @pick="(e) => addRef(draft.abilities, e)" />
        <div class="skill-head">Spells{{ me.school ? ` · ${me.school}` : '' }}</div>
        <div v-for="(a, i) in draft.spells" :key="'s' + i" class="edit-row">
          <button type="button" class="link" @click="gotoRules(a.key)">{{ a.name }}</button>
          <button type="button" class="x" :aria-label="`Remove ${a.name}`" @click="removeAt(draft.spells, i)">×</button>
        </div>
        <EntryPicker :token="token" type="spells" label="Add a spell…" @pick="(e) => addRef(draft.spells, e)" />
      </template>
      <template v-else>
        <button v-for="a in me.abilities" :key="a.key || a.name" class="entry-link" @click="gotoRules(a.key)">
          <span class="entry-link-name">{{ a.name }}</span>
          <span v-if="a.subtitle" class="entry-link-sub">{{ a.subtitle }}</span>
        </button>
        <template v-if="me.spells.length">
          <div class="skill-head">Spells{{ me.school ? ` · ${me.school}` : '' }}</div>
          <button v-for="sp in me.spells" :key="sp.key || sp.name" class="entry-link" @click="gotoRules(sp.key)">
            <span class="entry-link-name">{{ sp.name }}</span>
            <span v-if="sp.subtitle" class="entry-link-sub">{{ sp.subtitle }}</span>
          </button>
        </template>
      </template>
    </div>

    <div class="a-weapons block">
      <span class="banner">Weapon / shield</span>
      <p v-if="!eq.weapons.length" class="muted">None.</p>
      <div v-for="(w, i) in eq.weapons" :key="i" class="weapon">
        <div class="weapon-head">
          <button type="button" class="weapon-title" @click="gotoRules(w.key)">{{ w.name }}<span v-if="w.count > 1"> ×{{ w.count }}</span></button>
          <button v-if="editing" type="button" class="x" :aria-label="`Remove ${w.name}`" @click="removeAt(draft.weapons, i)">×</button>
        </div>
        <div class="stats">
          <span v-for="f in w.stats" :key="f.label"><b>{{ f.label }}</b>{{ f.value }}</span>
        </div>
        <div v-if="w.description" class="muted small">{{ w.description }}</div>
      </div>
      <EntryPicker v-if="editing" :token="token" type="weapons" label="Add a weapon…" @pick="addWeapon" />
    </div>

    <div class="a-armor block">
      <div class="gear">
        <span class="rating">{{ stat(eq.armor, 'Armor rating') || '–' }}</span>
        <span class="what">Armor</span>
        <input v-if="editing" class="edit name" :value="draft.armor?.name ?? ''" maxlength="80" aria-label="Armor" data-test="armor-input" @input="draft.armor = $event.target.value ? { ...(draft.armor ?? {}), name: $event.target.value } : null" />
        <span v-else class="name" data-test="armor">{{ eq.armor?.name ?? 'None' }}</span>
      </div>
      <EntryPicker v-if="editing" :token="token" type="armor" label="Pick armor…" @pick="(e) => chooseGear('armor', e)" />
      <div class="bane"><b>Bane on:</b> Sneaking · Evade · Acrobatics</div>
    </div>

    <div class="a-helmet block">
      <div class="gear">
        <span class="rating">{{ stat(eq.helmet, 'Armor rating') || '–' }}</span>
        <span class="what">Helmet</span>
        <input v-if="editing" class="edit name" :value="draft.helmet?.name ?? ''" maxlength="80" aria-label="Helmet" @input="draft.helmet = $event.target.value ? { ...(draft.helmet ?? {}), name: $event.target.value } : null" />
        <span v-else class="name" data-test="helmet">{{ eq.helmet?.name ?? 'None' }}</span>
      </div>
      <EntryPicker v-if="editing" :token="token" type="armor" label="Pick a helmet…" @pick="(e) => chooseGear('helmet', e)" />
      <div class="bane"><b>Bane on:</b> Awareness · Ranged attacks</div>
    </div>

    <div class="a-inv block">
      <div class="field-row">
        <span class="banner">Inventory</span>
        <span class="field" :class="{ error: overloaded, flagged: flagged('encumbrance') }" data-test="carrying">{{ me.derived.encumbrance.carried }}<span class="muted">/{{ me.derived.encumbrance.limit }}</span></span>
      </div>
      <div class="muted small" style="text-align: right">Encumbrance limit {{ me.derived.encumbrance.limit }}</div>
      <div v-for="(it, i) in (editing ? draft.inventory : me.equipment.inventory)" :key="'i' + i" class="inv-row"
        :class="{ 'drag-over': dragOver === i }"
        draggable="true"
        @dragstart="onDragStart(i, $event)"
        @dragover="onDragOver(i, $event)"
        @dragleave="dragOver = null"
        @drop="onDrop(i)"
        @dragend="onDragEnd">
        <span class="drag-handle" aria-hidden="true">⠿</span>
        <span class="n">{{ i + 1 }}</span>
        <input v-if="!it.key" v-model="it.name" class="edit" maxlength="80" :aria-label="`Item ${i + 1}`" data-test="item-input" />
        <span v-else class="inv-name">{{ it.name }}</span>
        <input type="number" class="edit count" min="1" max="999" :value="it.count ?? 1" :aria-label="`Number of ${it.name}`" @input="setInt(it, 'count', $event.target.value, 1, 999)" />
        <button type="button" class="x" :aria-label="`Remove ${it.name}`" @click="removeAt(draft.inventory, i)">×</button>
      </div>
      <div v-if="editing" class="inv-add">
        <EntryPicker :token="token" type="gear" label="Add an item…" @pick="(e) => addRef(draft.inventory, e)" />
        <button type="button" class="btn secondary small-btn" data-test="add-item" @click="addItem(draft.inventory)">+ Custom</button>
      </div>
    </div>

    <div class="a-coins block">
      <div class="coins-row">
        <template v-for="c in ['gold', 'silver', 'copper']" :key="c">
          <div class="coin-cell">
            <img :src="`/coins/${c}_coin_32.png`" :alt="c" class="coin-icon" />
            <input v-if="editing" type="number" class="edit coin-val" min="0" max="99999" :value="draft.coins[c]" :aria-label="c" :data-test="`${c}-input`" @input="setCoin(c, $event.target.value)" />
            <span v-else class="coin-val" :data-test="c">{{ eq.coins[c] }}</span>
          </div>
        </template>
      </div>
    </div>

    <div class="a-memento box">
      <span class="k">Memento</span>
      <textarea v-if="editing" v-model="draft.memento" class="edit" rows="3" maxlength="400" aria-label="Memento"></textarea>
      <template v-else>{{ me.about.memento }}</template>
    </div>
    <div class="a-tiny box">
      <span class="k">Tiny items</span>
      <template v-if="editing">
        <div v-for="(t, i) in draft.tiny_items" :key="'t' + i" class="edit-row">
          <input v-model="draft.tiny_items[i]" class="edit" maxlength="120" :aria-label="`Tiny item ${i + 1}`" />
          <button type="button" class="x" aria-label="Remove" @click="removeAt(draft.tiny_items, i)">×</button>
        </div>
        <button type="button" class="btn secondary small-btn" @click="draft.tiny_items.push('')">Add a tiny item</button>
      </template>
      <template v-else>
        <span v-for="(t, i) in eq.tinyItems" :key="i">{{ t }}<template v-if="i < eq.tinyItems.length - 1">, </template></span>
      </template>
    </div>

    <div v-if="editing || me.about.notes" class="a-about panel">
      <h2>Notes</h2>
      <textarea v-if="editing" v-model="draft.notes" class="edit" rows="4" maxlength="8000" aria-label="Notes"></textarea>
      <p v-else style="white-space: pre-wrap; margin: 0">{{ me.about.notes }}</p>
    </div>
  </section>
</template>
