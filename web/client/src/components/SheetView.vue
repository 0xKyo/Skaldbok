<script setup>
// The character sheet, laid out like the first page of the printed one (the green page) and arranged for a phone: the same blocks in
// the order a player needs them at the table, and the printed three-column layout on a wide screen.
//
// A player can edit all of it (kin, profession and school aside: those are chosen when the GM creates the character). Changes go to the
// server a moment after they are made, field by field. Breaking a rule is allowed but shows in red, and the GM decides.
import { computed, onBeforeUnmount, ref, watch } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import { t } from '../i18n.js';
import { adopt, clone, diffDoc, issueFor } from '../lib/sheetdoc.js';
import EntryPicker from './EntryPicker.vue';
import InlineCard from './InlineCard.vue';
import DeathRolls from './DeathRolls.vue';
import NumberField from './NumberField.vue';
import Points from './Points.vue';

const SAVE_DELAY_MS = 400;
const RETRY_MS = 3000;

const props = defineProps({
  me: { type: Object, required: true },
  token: { type: String, default: '' },
  patchPath: { type: String, default: '/me' }, // '/me' for player, '/gm/characters/:id' for GM
});
// the GM reads the rules through /gm/*, a player through the plain routes
const apiPrefix = computed(() => (props.patchPath.startsWith('/gm/') ? '/gm' : ''));
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
// The server names the things it checks in English ("id": a skill's, a stat's), and shows them in the page's language ("name", "label").
const idOf = (x) => x.id ?? x.name ?? x.label;
const attrShort = (key) => props.me.attributes.find((a) => a.key === key)?.short ?? key;
// what the worn armor and helmet do by themselves: a bane on some skills, and on ranged attacks
const wornBanes = computed(() => new Set([props.me.equipment.armor, props.me.equipment.helmet].flatMap((i) => i?.banes ?? []).map((b) => b.toLowerCase())));
const hasBane = (s) => activeConditions.value.some((c) => c.attribute === s.attribute) || wornBanes.value.has(idOf(s).toLowerCase());
const rangedBane = computed(() => wornBanes.value.has('ranged attacks'));
const meleeBane = computed(() => wornBanes.value.has('melee attacks'));
// a bane on attacks is not written as text: the weapons it hits are outlined in red, like the skills that have a bane
const weaponBane = (w) => (w.ranged ? rangedBane.value : meleeBane.value);
const skillBanes = (item) => (item?.banes ?? []).filter((b) => !/\battacks$/i.test(b)).map((b) => item.baneLabels?.[b] ?? b);
const bonus = (v) => (v ? `+${v}` : '–');
// A weapon's damage is rolled with the damage bonus of its kind: strength for a melee weapon, agility for a ranged one ("" when there is none).
// The server lists the damage bonus of each attribute that has one, as {key, short, value}.
const damageBonusFor = (key) => props.me.derived.damageBonus.find((b) => b.key === key)?.value ?? '';
const damageAttribute = (w) => (w.ranged ? 'agility' : 'strength');
const damageBonusOf = (w) => damageBonusFor(damageAttribute(w));
const eq = computed(() => props.me.equipment);
const stat = (item, id) => item?.stats.find((f) => (f.id ?? f.label) === id)?.value ?? '';
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
      saveError.value = t('Offline: your change will be sent when the connection is back.');
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

// hit points and the death rolls: at 0 the rolls take the place of the hit points; three successes bring the points back (the bar shows
// again, still at 0), three failures are death. Going above 0 starts the rolls over.
const hpNow = computed(() => (editing.value ? draft.value.hp : props.me.hp.current));
const deaths = computed(() => (editing.value
  ? { successes: draft.value.death_successes ?? 0, failures: draft.value.death_failures ?? 0 }
  : { successes: props.me.death?.successes ?? 0, failures: props.me.death?.failures ?? 0 }));
const onDeathRolls = computed(() => hpNow.value <= 0 && deaths.value.successes < 3);
function setHp(value) {
  draft.value.hp = num(value, 0, 999);
  if (draft.value.hp > 0) {
    if (draft.value.death_successes) draft.value.death_successes = 0;
    if (draft.value.death_failures) draft.value.death_failures = 0;
  }
}
const restTaken = (key) => (editing.value ? !!draft.value[key] : !!props.me.rest?.[key === 'round_rest' ? 'round' : 'stretch']);
const setRest = (key, on) => (draft.value[key] = on);
const setDeath = (key, value) => (draft.value[key] = num(value, 0, 3));

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
// Kin, profession and school are the GM's to change: only a GM's sheet carries them in its editable document.
const identityEditable = computed(() => !!draft.value && 'kin' in draft.value);
const catalog = ref(null);
watch(() => editing.value && identityEditable.value, async (on) => {
  if (!on || catalog.value) return;
  try { catalog.value = await apiGet(`${apiPrefix.value}/creation`, props.token); } catch { /* the current kin and profession are still shown */ }
}, { immediate: true });
const cardOptions = (list, current) => {
  const options = (list ?? []).map((c) => ({ key: c.key, name: c.title }));
  return current?.name && !options.some((o) => o.key === current.key) ? [{ key: current.key ?? '', name: current.name }, ...options] : options;
};
const kinOptions = computed(() => cardOptions(catalog.value?.kins, draft.value?.kin));
const professionOptions = computed(() => cardOptions(catalog.value?.professions, draft.value?.profession));
const schoolsOfDraft = computed(() => catalog.value?.professions.find((p) => p.key === draft.value?.profession?.key)?.schools ?? []);
// the catalog names a school in English; the sheet's document, like the page, shows it in the page's language
const catalogLabel = (name) => catalog.value?.labels?.[name] ?? name;
function setKin(key) {
  const k = kinOptions.value.find((o) => o.key === key);
  if (k) draft.value.kin = { key: k.key, name: k.name };
}
function setProfession(key) {
  const p = professionOptions.value.find((o) => o.key === key);
  if (!p) return;
  draft.value.profession = { key: p.key, name: p.name };
  draft.value.school = catalogLabel(catalog.value?.professions.find((x) => x.key === key)?.schools[0] ?? '');     // only a mage has one
}

// On a phone the sheet is tabs, so little scrolling is needed: the stats (attributes, HP and WP), the skills, the abilities, the gear and the details
// (who the character is); a wide screen shows them all at once.
const sheetTabs = computed(() => [{ id: 'stats', label: t('Stats') }, { id: 'skills', label: t('Skills') }, { id: 'abilities', label: t('Abilities') }, { id: 'gear', label: t('Gear') }, { id: 'details', label: t('Details') }]);
const skillGroups = computed(() => [
  { id: 'core', title: t('Skills'), list: coreSkills.value },
  { id: 'weapon', title: t('Weapon skills'), list: weaponSkills.value },
  { id: 'other', title: t('Secondary skills'), list: otherSkills.value },
]);
const sheetTab = ref('stats');

const addItem = (list) => list.push({ name: '', count: 1 });
const addRef = (list, entry) => list.push({ key: entry.key, name: entry.name });
const addWeapon = (entry) => draft.value.weapons.push({ key: entry.key, name: entry.name });
// Armor and helmet are dropdowns of what the rules have for each slot (an armor card with no slot is body armor).
const armorCards = ref([]);
watch(editing, async (on) => {
  if (!on || armorCards.value.length) return;
  try { armorCards.value = (await apiGet(`${apiPrefix.value}/content/armor`, props.token)).entries ?? []; } catch { /* what is worn is still shown */ }
}, { immediate: true });
const slotOptions = (slot, current) => {
  const options = armorCards.value.filter((c) => (c.slot || 'armor') === slot).map((c) => ({ value: c.key, name: c.name }));
  // something written by hand, or from a pack that is gone, stays on the list as it is
  return current?.name && !options.some((o) => o.value === current.key) ? [{ value: current.key || `name:${current.name}`, name: current.name }, ...options] : options;
};
const armorOptions = computed(() => slotOptions('armor', draft.value?.armor));
const helmetOptions = computed(() => slotOptions('helmet', draft.value?.helmet));
function chooseGear(which, value) {
  const options = which === 'armor' ? armorOptions.value : helmetOptions.value;
  const picked = options.find((o) => o.value === value);
  if (!picked) draft.value[which] = null;
  else if (value.startsWith('name:')) draft.value[which] = { name: picked.name };
  else draft.value[which] = { key: picked.value, name: picked.name };
}
</script>

<template>
  <section class="sheet" :class="{ editing }" :data-tab="sheetTab" :aria-label="t('Character sheet')">
    <div v-if="me.locked || lockedNow || saveError" class="a-tools">
      <span v-if="me.locked || lockedNow" class="locked-note" data-test="locked">{{ t('Your GM has locked your sheet.') }}</span>
      <span v-if="saveError" class="error small" data-test="save-error">{{ saveError }}</span>
    </div>

    <div v-if="issues.length" class="a-issues" data-test="issues">
      <div v-for="i in issues" :key="i.key" class="issue" :class="i.status" data-test="issue">
        <b>{{ i.status === 'rejected' ? t('Your GM says no') : t('Rule check') }}</b>
        {{ i.message }}
        <span class="muted small">{{ i.status === 'rejected' ? t(' — change it back yourself.') : t(' — your GM will look at it.') }}</span>
      </div>
    </div>

    <div class="a-name scroll">
      <template v-if="editing">
        <div class="name-line">
          <input v-model="draft.name" class="edit the-name" maxlength="80" :aria-label="t('Name')" data-test="name" />
          <input v-model="draft.nickname" class="edit the-nick" maxlength="80" :placeholder="t('Nickname')" :aria-label="t('Nickname')" />
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
      <div class="line"><span class="k">{{ t('Player') }}</span>{{ me.player }}</div>
      <div class="line">
        <span class="k">{{ t('Kin') }}</span>
        <select v-if="editing && identityEditable" :value="draft.kin?.key ?? ''" class="edit" :aria-label="t('Kin')" data-test="kin-select" @change="setKin($event.target.value)">
          <option v-for="o in kinOptions" :key="o.key" :value="o.key">{{ o.name }}</option>
        </select>
        <span v-else class="kin-name">{{ me.kin.name }}</span>
      </div>
      <div class="line">
        <span class="k">{{ t('Age') }}</span>
        <select v-if="editing" v-model="draft.age" class="edit age-select" :aria-label="t('Age')" data-test="age-select">
          <option value="young">{{ t('Young') }}</option>
          <option value="adult">{{ t('Adult') }}</option>
          <option value="old">{{ t('Old') }}</option>
        </select>
        <span v-else>{{ me.age.label }}</span>
      </div>
      <div class="line line-top">
        <span class="k">{{ t('Appearance') }}</span>
        <textarea v-if="editing" v-model="draft.appearance" class="edit" rows="3" maxlength="1500" :aria-label="t('Appearance')"></textarea>
        <span v-else class="line-text">{{ me.about.appearance }}</span>
      </div>
    </div>

    <div class="a-appear lines">
      <div class="line">
        <span class="k">{{ t('Profession') }}</span>
        <select v-if="editing && identityEditable" :value="draft.profession?.key ?? ''" class="edit" :aria-label="t('Profession')" data-test="profession-select" @change="setProfession($event.target.value)">
          <option v-for="o in professionOptions" :key="o.key" :value="o.key">{{ o.name }}</option>
        </select>
        <template v-else>{{ me.profession.name }}</template>
      </div>
      <div v-if="editing && identityEditable && schoolsOfDraft.length" class="line">
        <span class="k">{{ t('School') }}</span>
        <select v-model="draft.school" class="edit" :aria-label="t('School of magic')" data-test="school-select">
          <option v-for="s in schoolsOfDraft" :key="s" :value="catalogLabel(s)">{{ catalogLabel(s) }}</option>
        </select>
      </div>
      <div v-else-if="me.school" class="line"><span class="k">{{ t('School') }}</span>{{ me.school }}</div>
      <div class="line line-top">
        <span class="k">{{ t('Weakness') }}</span>
        <textarea v-if="editing" v-model="draft.weakness" class="edit" rows="1" maxlength="400" :aria-label="t('Weakness')"></textarea>
        <span v-else class="line-text">{{ me.about.weakness }}</span>
      </div>
    </div>

    <div class="a-attrs">
      <div class="gems" data-test="conditions">
        <div v-for="a in me.attributes" :key="a.key" class="gem" :class="{ flagged: flagged('attr:' + a.key) }" :title="a.name">
          <span class="abbr">{{ a.short ?? a.key }}</span>
          <span class="ring">
            <NumberField v-if="editing" class="edit ring-input" :min="0" :max="99" :model-value="draft.attributes[a.key]" :aria-label="a.name" data-test="attribute-input" @update:model-value="(v) => setAttribute(a.key, v)" />
            <b v-else data-test="attribute">{{ a.value }}</b>
          </span>
          <span class="base">{{ t('base chance {n}', { n: a.baseChance }) }}</span>
          <component
            :is="editing ? 'button' : 'span'"
            v-if="conditionOf(a.key)"
            class="cond"
            :class="{ on: editing ? isCondition(conditionOf(a.key).name) : conditionOf(a.key).active }"
            :type="editing ? 'button' : undefined"
            :title="t('A bane on {attr} and the skills based on it', { attr: a.short ?? a.key })"
            @click="editing && toggleCondition(conditionOf(a.key).name)"
          >
            <span class="dia"></span>{{ conditionOf(a.key).name }}
          </component>
        </div>
      </div>
    </div>

    <!-- what the attributes give: the damage bonuses and the movement, to the right of them -->
    <div class="a-derived block">
      <div v-for="b in me.derived.damageBonus" :key="b.key" class="field-row"><span class="banner">{{ t('Damage bon. {attr}', { attr: b.short }) }}</span><span class="field" :data-test="`damage-${b.key}`">{{ bonus(b.value) }}</span></div>
      <div class="field-row"><span class="banner">{{ t('Movement') }}</span><span class="field" data-test="movement">{{ me.derived.movement ?? '?' }}</span></div>
    </div>
    <div class="a-points">
    <div class="a-hp">
      <!-- at 0 the hit points give way to the death rolls: three successes bring them back, three failures and the character is dead -->
      <DeathRolls
        v-if="onDeathRolls"
        :successes="deaths.successes"
        :failures="deaths.failures"
        :editable="editing"
        @update:successes="(v) => setDeath('death_successes', v)"
        @update:failures="(v) => setDeath('death_failures', v)"
      />
      <Points
        v-else
        :label="t('HP')"
        :current="hpNow"
        :max="me.hp.max"
        kind="hp"
        :editable="editing"
        :flagged="flagged('hp') || flagged('hpmax')"
        @update:current="setHp"
      />
    </div>
    <div class="a-wp">
      <Points
        :label="t('WP')"
        :current="editing ? draft.wp : me.wp.current"
        :max="me.wp.max"
        kind="wp"
        :editable="editing"
        :flagged="flagged('wp') || flagged('wpmax')"
        @update:current="(v) => (draft.wp = num(v, 0, 999))"
      />
    </div>
    </div>

    <!-- the rests taken since the shift began: the Master clears them when the next shift starts -->
    <div class="a-rest box" data-test="rests">
      <label class="rest-check"><input type="checkbox" :checked="restTaken('round_rest')" :disabled="!editing" data-test="round-rest" @change="setRest('round_rest', $event.target.checked)" /> {{ t('Round Rest') }}</label>
      <label class="rest-check"><input type="checkbox" :checked="restTaken('stretch_rest')" :disabled="!editing" data-test="stretch-rest" @change="setRest('stretch_rest', $event.target.checked)" /> {{ t('Stretch Rest') }}</label>
    </div>

    <div class="a-subtabs subtabs" role="group" :aria-label="t('Sheet section')" data-test="sheet-tabs">
      <button v-for="tab in sheetTabs" :key="tab.id" type="button" :aria-pressed="sheetTab === tab.id" :data-test="`sheet-tab-${tab.id}`" @click="sheetTab = tab.id">{{ tab.label }}</button>
    </div>

    <div class="a-skills parchment" data-test="skills">
      <span class="banner">{{ t('Skills') }}</span>
      <div class="skill-cols">
        <template v-for="group in skillGroups" :key="group.id">
          <div v-if="group.list.length" :class="group.id === 'core' ? '' : 'skill-group'">
            <div class="skill-head">{{ group.title }}</div>
            <div v-for="s in group.list" :key="s.name" class="skill" :class="{ untrained: editing ? !isTrained(s) : !s.trained, flagged: flagged('skill:' + idOf(s)) || hasBane(s) }">
              <template v-if="editing">
                <button type="button" class="dia-btn" :aria-pressed="isMarked(s)" :aria-label="t('Advancement mark for {name}', { name: s.name })" @click="toggleMark(s)"><span class="dia" :class="{ on: isMarked(s) }"></span></button>
                <NumberField class="edit lvl-input" :min="0" :max="99" :model-value="levelOf(s)" :aria-label="t('{name} level', { name: s.name })" data-test="skill-input" @update:model-value="(v) => setLevel(s, v)" />
                <button type="button" class="skill-link" @click="gotoRules(s.key)">{{ s.name }} <span class="attr">({{ s.attributeLabel ?? s.attribute }})</span></button>
              </template>
              <template v-else>
                <span class="dia" :class="{ on: s.marked }" :title="s.marked ? t('Advancement mark') : ''"></span>
                <span class="lvl" data-test="skill-level">{{ s.level }}</span>
                <button type="button" class="skill-link" @click="gotoRules(s.key)">{{ s.name }} <span class="attr">({{ s.attributeLabel ?? s.attribute }})</span></button>
              </template>
            </div>
          </div>
        </template>
      </div>
      <p v-if="!skillsShown.length" class="muted">{{ t('No skills to show.') }}</p>
      <p class="muted small" style="margin: 10px 0 0">{{ t('A skill is rolled with a D20: at or below its level succeeds, 1 is a dragon, 20 is a demon. Untrained skills use their base chance.') }}</p>
    </div>

    <div class="a-abil block">
      <span class="banner">{{ t('Abilities & spells') }}</span>
      <p v-if="!me.abilities.length && !me.spells.length && !editing" class="muted">{{ t('None yet.') }}</p>
      <template v-if="editing">
        <div v-for="(a, i) in draft.abilities" :key="'a' + i" class="edit-row">
          <button type="button" class="link" @click="gotoRules(a.key)">{{ a.name }}</button>
          <button type="button" class="x" :aria-label="t('Remove {name}', { name: a.name })" @click="removeAt(draft.abilities, i)">×</button>
        </div>
        <EntryPicker :token="token" :prefix="apiPrefix" type="abilities" :label="t('Add an ability…')" @pick="(e) => addRef(draft.abilities, e)" />
        <div class="skill-head">{{ t('Spells') }}{{ me.school ? ` · ${me.school}` : '' }}</div>
        <div v-for="(a, i) in draft.spells" :key="'s' + i" class="edit-row">
          <button type="button" class="link" @click="gotoRules(a.key)">{{ a.name }}</button>
          <button type="button" class="x" :aria-label="t('Remove {name}', { name: a.name })" @click="removeAt(draft.spells, i)">×</button>
        </div>
        <EntryPicker :token="token" :prefix="apiPrefix" type="spells" :label="t('Add a spell…')" @pick="(e) => addRef(draft.spells, e)" />
      </template>
      <template v-else>
        <button v-for="a in me.abilities" :key="a.key || a.name" class="entry-link" @click="gotoRules(a.key)">
          <span class="entry-link-name">{{ a.name }}</span>
          <span v-if="a.subtitle" class="entry-link-sub">{{ a.subtitle }}</span>
        </button>
        <template v-if="me.spells.length">
          <div class="skill-head">{{ t('Spells') }}{{ me.school ? ` · ${me.school}` : '' }}</div>
          <!-- a spell is a card that opens where it is, without leaving the sheet -->
          <InlineCard v-for="sp in me.spells" :key="sp.key || sp.name" :entry="sp" type="spells" :token="token" :prefix="apiPrefix" @follow="gotoRules" />
        </template>
      </template>
    </div>

    <div class="a-weapons block">
      <span class="banner">{{ t('Weapon / shield') }}</span>
      <p v-if="!eq.weapons.length" class="muted">{{ t('None.') }}</p>
      <div v-for="(w, i) in eq.weapons" :key="i" class="weapon" :class="{ flagged: weaponBane(w) }" :title="weaponBane(w) ? t('A bane on these attacks') : undefined" data-test="weapon">
        <div class="weapon-head">
          <button type="button" class="weapon-title" @click="gotoRules(w.key)">{{ w.name }}<span v-if="w.count > 1"> ×{{ w.count }}</span></button>
          <button v-if="editing" type="button" class="x" :aria-label="t('Remove {name}', { name: w.name })" @click="removeAt(draft.weapons, i)">×</button>
        </div>
        <div class="stats">
          <span v-for="f in w.stats" :key="f.label"><b>{{ f.label }}</b>{{ f.value }}<span v-if="(f.id ?? f.label) === 'Damage' && damageBonusOf(w)" class="dmg-bonus" :title="t('Damage bonus {attr}', { attr: attrShort(damageAttribute(w)) })" data-test="weapon-damage-bonus">+{{ damageBonusOf(w) }}</span></span>
        </div>
        <div v-if="w.description" class="muted small">{{ w.description }}</div>
      </div>
      <EntryPicker v-if="editing" :token="token" :prefix="apiPrefix" type="weapons" :label="t('Add a weapon…')" @pick="addWeapon" />
    </div>

    <div class="a-armor block">
      <div class="gear">
        <span class="rating">{{ stat(eq.armor, 'Armor rating') || '–' }}</span>
        <span class="what">{{ t('Armor') }}</span>
        <select v-if="editing" class="edit name" :value="draft.armor?.key || (draft.armor?.name ? `name:${draft.armor.name}` : '')" :aria-label="t('Armor')" data-test="armor-select" @change="chooseGear('armor', $event.target.value)">
          <option value="">{{ t('None') }}</option>
          <option v-for="o in armorOptions" :key="o.value" :value="o.value">{{ o.name }}</option>
        </select>
        <span v-else class="name" data-test="armor">{{ eq.armor?.name ?? t('None') }}</span>
      </div>
      <div v-if="skillBanes(eq.armor).length" class="bane" data-test="armor-bane"><b>{{ t('Bane on:') }}</b> {{ skillBanes(eq.armor).join(' · ') }}</div>
    </div>

    <div class="a-helmet block">
      <div class="gear">
        <span class="rating">{{ stat(eq.helmet, 'Armor rating') || '–' }}</span>
        <span class="what">{{ t('Helmet') }}</span>
        <select v-if="editing" class="edit name" :value="draft.helmet?.key || (draft.helmet?.name ? `name:${draft.helmet.name}` : '')" :aria-label="t('Helmet')" data-test="helmet-select" @change="chooseGear('helmet', $event.target.value)">
          <option value="">{{ t('None') }}</option>
          <option v-for="o in helmetOptions" :key="o.value" :value="o.value">{{ o.name }}</option>
        </select>
        <span v-else class="name" data-test="helmet">{{ eq.helmet?.name ?? t('None') }}</span>
      </div>
      <div v-if="skillBanes(eq.helmet).length" class="bane" data-test="helmet-bane"><b>{{ t('Bane on:') }}</b> {{ skillBanes(eq.helmet).join(' · ') }}</div>
    </div>

    <div class="a-inv block">
      <div class="field-row">
        <span class="banner">{{ t('Inventory') }}</span>
        <span class="field" :class="{ error: overloaded, flagged: flagged('encumbrance') }" data-test="carrying">{{ me.derived.encumbrance.carried }}<span class="muted">/{{ me.derived.encumbrance.limit }}</span></span>
      </div>
      <div v-for="(it, i) in (editing ? draft.inventory : me.equipment.inventory)" :key="'i' + i" class="inv-row"
        :class="{ 'drag-over': dragOver === i }"
        draggable="true"
        @dragstart="onDragStart(i, $event)"
        @dragover="onDragOver(i, $event)"
        @dragleave="dragOver = null"
        @drop="onDrop(i)"
        @dragend="onDragEnd">
        <span class="drag-handle" aria-hidden="true">⠿</span>
        <input v-if="!it.key" v-model="it.name" class="edit" maxlength="80" :aria-label="t('Item {n}', { n: i + 1 })" data-test="item-input" />
        <span v-else class="inv-name">{{ it.name }}</span>
        <span v-if="!editing && (it.count ?? 1) > 1" class="inv-count" data-test="item-count">×{{ it.count }}</span>
        <NumberField v-if="editing" class="edit count" :min="1" :max="999" :model-value="it.count ?? 1" :aria-label="t('Number of {name}', { name: it.name })" @update:model-value="(v) => (it.count = v)" />
        <button type="button" class="x" :aria-label="t('Remove {name}', { name: it.name })" @click="removeAt(draft.inventory, i)">×</button>
      </div>
      <div v-if="editing" class="inv-add">
        <EntryPicker :token="token" :prefix="apiPrefix" type="gear" :label="t('Add an item…')" @pick="(e) => addRef(draft.inventory, e)" />
        <button type="button" class="btn secondary small-btn" data-test="add-item" @click="addItem(draft.inventory)">{{ t('+ Custom') }}</button>
      </div>
    </div>

    <div class="a-coins block">
      <div class="coins-row">
        <template v-for="c in ['gold', 'silver', 'copper']" :key="c">
          <div class="coin-cell">
            <img :src="`/coins/${c}_coin_32.png`" :alt="t(c)" class="coin-icon" />
            <NumberField v-if="editing" class="edit coin-val" :min="0" :max="99999" :model-value="draft.coins[c]" :aria-label="t(c)" :data-test="`${c}-input`" @update:model-value="(v) => setCoin(c, v)" />
            <span v-else class="coin-val" :data-test="c">{{ eq.coins[c] }}</span>
          </div>
        </template>
      </div>
    </div>

    <div class="a-memento box">
      <span class="k">{{ t('Memento') }}</span>
      <textarea v-if="editing" v-model="draft.memento" class="edit" rows="3" maxlength="400" :aria-label="t('Memento')"></textarea>
      <template v-else>{{ me.about.memento }}</template>
    </div>
    <div class="a-tiny box">
      <span class="k">{{ t('Tiny items') }}</span>
      <template v-if="editing">
        <div v-for="(tiny, i) in draft.tiny_items" :key="'t' + i" class="edit-row">
          <input v-model="draft.tiny_items[i]" class="edit" maxlength="120" :aria-label="t('Tiny item {n}', { n: i + 1 })" />
          <button type="button" class="x" :aria-label="t('Remove')" @click="removeAt(draft.tiny_items, i)">×</button>
        </div>
        <div class="inv-add">
          <EntryPicker :token="token" :prefix="apiPrefix" type="tiny" :label="t('Add a tiny item…')" @pick="(e) => draft.tiny_items.push(e.name)" />
          <button type="button" class="btn secondary small-btn" @click="draft.tiny_items.push('')">{{ t('+ Custom') }}</button>
        </div>
      </template>
      <template v-else>
        <span v-for="(tiny, i) in eq.tinyItems" :key="i">{{ tiny }}<template v-if="i < eq.tinyItems.length - 1">, </template></span>
      </template>
    </div>

    <div v-if="editing || me.about.notes" class="a-about panel">
      <h2>{{ t('Notes') }}</h2>
      <textarea v-if="editing" v-model="draft.notes" class="edit" rows="4" maxlength="8000" :aria-label="t('Notes')"></textarea>
      <p v-else style="white-space: pre-wrap; margin: 0">{{ me.about.notes }}</p>
    </div>
  </section>
</template>
