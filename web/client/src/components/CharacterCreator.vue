<script setup>
// The GM's character creator: the Rulebook's steps (kin, profession, age, attributes, skills, ability & magic, gear, name & details,
// review). It only collects choices; what is valid and the sheet that comes out are the server's (POST /gm/characters {creation}).
import { computed, onMounted, reactive, ref, watch } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import ChoiceLayout from './ChoiceLayout.vue';

const props = defineProps({
  token: { type: String, required: true },
  random: { type: Boolean, default: false },      // open already filled with a random character, on the review step
});
const emit = defineEmits(['created', 'cancel']);

const STEPS = ['Kin', 'Profession', 'Age', 'Attributes', 'Skills', 'Ability & magic', 'Gear', 'Name & details', 'Review'];

const catalog = ref(null);
const content = reactive({ kin: [], professions: [], abilities: [], weapons: [], armor: [], gear: [] });      // the Reference's own cards, shown beside each list of options
const loadError = ref('');
const step = ref(0);
const furthest = ref(0);
const note = ref('');
const busy = ref(false);
const preview = ref(null);
const swap = reactive({ a: 0, b: 1 });

const blank = () => ({
  kin: '', profession: '', age: 'adult', school: '', rolled: [0, 0, 0, 0, 0, 0],
  professionSkills: [], freeSkills: [], heroicAbility: '', spells: [], gearSet: -1, gear: [], gearCustom: false, customGear: [],
  name: '', nickname: '', player: '', weakness: '', memento: '', appearance: '',
});
const cr = reactive(blank());

const d = (sides) => 1 + Math.floor(Math.random() * sides);
const pick = (list) => list[d(list.length) - 1];
const rollAttribute = () => {
  const dice = [d(6), d(6), d(6), d(6)];
  return dice.reduce((a, b) => a + b, 0) - Math.min(...dice);
};

onMounted(async () => {
  try {
    catalog.value = await apiGet('/gm/creation', props.token);
    await Promise.all(Object.keys(content).map(async (type) => {
      try { content[type] = (await apiGet(`/gm/content/${type}`, props.token)).entries ?? []; } catch { /* the data of the catalog is shown instead */ }
    }));
    if (props.random) await randomize();
    else {
      chooseKin(catalog.value.kins[0]?.key ?? '');
      chooseProfession(catalog.value.professions[0]?.key ?? '');
    }
  } catch (e) {
    loadError.value = e instanceof ApiError ? e.message : 'Could not load the creator.';
  }
});

// ---- what the choices mean -------------------------------------------------------------------------
const kin = computed(() => catalog.value?.kins.find((k) => k.key === cr.kin) ?? null);
const prof = computed(() => catalog.value?.professions.find((p) => p.key === cr.profession) ?? null);
const ageInfo = computed(() => catalog.value?.ages.find((a) => a.id === cr.age) ?? null);
const isMage = computed(() => !!prof.value?.schools.length);
const options = computed(() => {
  if (!prof.value) return [];
  return isMage.value ? (prof.value.skillsBySchool[cr.school] ?? []) : prof.value.skills;
});
const need = computed(() => Math.min(catalog.value?.professionSkills ?? 6, options.value.length));
const wanted = computed(() => Math.max(0, (ageInfo.value?.trainedSkills ?? 10) - need.value));
const finalAttrs = computed(() => cr.rolled.map((v, i) => Math.min(18, Math.max(1, Math.max(3, v) + (ageInfo.value?.attrMod[i] ?? 0)))));
const chanceOf = (score) => catalog.value?.baseChance[score] ?? 0;
const levelOf = (skill) => {
  const a = catalog.value?.skillAttributes[skill];
  const i = catalog.value?.attributes.findIndex((x) => x.short === a) ?? -1;
  return i < 0 ? 0 : Math.min(18, 2 * chanceOf(finalAttrs.value[i]));
};
const freeChoices = computed(() => catalog.value?.selectableSkills ?? []);
const magicChoices = (tricks) => {
  if (!catalog.value || !prof.value) return [];
  const rank = prof.value.magic.rank;
  return catalog.value.spells.filter((s) => s.trick === tricks && (s.school === cr.school || s.school === 'General Magic') && (tricks || !rank || s.rank === rank));
};
const spellCount = (tricks) => cr.spells.filter((k) => catalog.value.spells.find((s) => s.key === k)?.trick === tricks).length;
const heroicList = computed(() => prof.value?.heroic ?? []);
const gearSets = computed(() => prof.value?.gearSets ?? []);
// the options of each list, and the card shown beside them: the Reference's own entry when it is loaded, else what the catalog knows
const kinRows = computed(() => catalog.value.kins.map((k) => ({ key: k.key, title: k.title, sub: k.source })));
const professionRows = computed(() => catalog.value.professions.map((p) => ({ key: p.key, title: p.title, sub: p.source })));
const ageRows = computed(() => catalog.value.ages.map((a) => ({ key: a.id, title: a.label })));
const heroicRows = computed(() => heroicList.value.map((h) => ({ key: h.name, title: h.name })));
const gearRows = computed(() => [...gearSets.value.map((g, i) => ({ key: String(i), title: `Set ${i + 1}` })), { key: 'custom', title: 'Custom' }]);
const gearChoice = computed(() => (cr.gearCustom ? 'custom' : cr.gearSet >= 0 ? String(cr.gearSet) : ''));
const ageDetail = computed(() => {
  const a = ageInfo.value;
  if (!a) return null;
  const mods = catalog.value.attributes.map((x, i) => (a.attrMod[i] ? `${x.short} ${a.attrMod[i] > 0 ? '+' : ''}${a.attrMod[i]}` : '')).filter(Boolean).join(', ');
  return { name: a.label, body: a.summary, fields: [{ label: 'Trained skills', value: String(a.trainedSkills) }, { label: 'Attributes', value: mods || 'no change' }] };
});
const gearDetail = computed(() => {
  if (cr.gearCustom) return { name: 'Custom', body: 'Choose whatever you want to carry, from the weapons, armor and gear of the rules. What it all costs is added up below.' };
  return cr.gearSet >= 0 ? { name: `Set ${cr.gearSet + 1}`, body: gearSets.value[cr.gearSet].text } : null;
});

// ---- custom gear: any weapon, armor or gear of the rules, with what it costs added up ----
const customQuery = ref('');
const customKind = ref('all');
const COPPER = { gold: 100, silver: 10, copper: 1 };
function costOf(entry) {                                                    // in copper; null when the price is not a plain amount
  const text = entry.fields?.find((f) => f.label === 'Cost')?.value ?? '';
  const m = /^(\d+)\s*(gold|silver|copper)$/i.exec(text.trim());
  return m ? Number(m[1]) * COPPER[m[2].toLowerCase()] : null;
}
const priceText = (copper) => {
  if (copper === null) return '—';
  const parts = [[Math.floor(copper / 100), 'gold'], [Math.floor((copper % 100) / 10), 'silver'], [copper % 10, 'copper']].filter(([n]) => n > 0);
  return parts.length ? parts.map(([n, u]) => `${n} ${u}`).join(', ') : '0';
};
const catalogItems = computed(() => [['weapons', content.weapons], ['armor', content.armor], ['gear', content.gear]]
  .flatMap(([kind, list]) => list.map((e) => ({ key: e.key, name: e.name, kind, sub: e.subtitle ?? '', copper: costOf(e) }))));
const customChoices = computed(() => {
  const q = customQuery.value.trim().toLowerCase();
  return catalogItems.value.filter((i) => (customKind.value === 'all' || i.kind === customKind.value) && (!q || i.name.toLowerCase().includes(q)));
});
const customList = computed(() => cr.customGear.map((g) => ({ ...g, ...(catalogItems.value.find((i) => i.key === g.key) ?? { name: g.key, copper: null }) })));
const customTotal = computed(() => customList.value.reduce((sum, i) => sum + (i.copper ?? 0) * i.count, 0));
const customUnpriced = computed(() => customList.value.filter((i) => i.copper === null).length);
function chooseCustomGear() {
  cr.gearCustom = true;
  cr.gearSet = -1;
  cr.gear = [];
}
function addCustom(key) {
  const held = cr.customGear.find((g) => g.key === key);
  if (held) held.count += 1;
  else cr.customGear.push({ key, count: 1 });
}
function setCustomCount(key, count) {
  if (count < 1) cr.customGear = cr.customGear.filter((g) => g.key !== key);
  else cr.customGear.find((g) => g.key === key).count = Math.min(99, count);
}
function detailOf(list, card, name) {
  if (card) return list.find((e) => e.key === card.key) ?? { name: card.title, body: card.body ?? '', fields: card.fields ?? [] };
  if (!name) return null;
  const found = list.find((e) => (e.name ?? '').toLowerCase() === name.toLowerCase());
  const known = catalog.value.heroicAbilities.find((h) => h.name === name);
  return found ?? (known ? { name, body: known.body, fields: known.fields } : { name, body: '' });
}
const allAttrsSet = computed(() => cr.rolled.every((v) => v >= 3 && v <= 18));
const anyScoreBad = computed(() => cr.rolled.some(scoreBad));

function stepDone(i) {
  switch (i) {
    case 0: return !!kin.value;
    case 1: return !!prof.value && (!isMage.value || !!cr.school);
    case 2: return true;
    case 3: return allAttrsSet.value;
    case 4: return !!prof.value && cr.professionSkills.length === need.value && cr.freeSkills.length === wanted.value;
    case 5:
      if (!prof.value) return false;
      if (isMage.value) return spellCount(false) === prof.value.magic.spells && spellCount(true) === prof.value.magic.tricks;
      return !heroicList.value.length || !!cr.heroicAbility;
    case 6: return cr.gearCustom || cr.gearSet >= 0 || !gearSets.value.length;
    case 7: return !!cr.name.trim();
    default: return !preview.value?.problems.length;
  }
}
const reached = (i) => i <= furthest.value;
const goto = (i) => { if (reached(i)) step.value = i; };
const next = () => { step.value += 1; furthest.value = Math.max(furthest.value, step.value); };

// ---- choosing ---------------------------------------------------------------------------------------
function chooseKin(key) {
  cr.kin = key;
  cr.name = '';                                   // the names offered depend on the kin
}

function chooseProfession(key) {
  if (cr.profession === key) return;
  Object.assign(cr, { profession: key, school: '', professionSkills: [], freeSkills: [], heroicAbility: '', spells: [], gearSet: -1, gear: [], gearCustom: false, customGear: [], nickname: '' });
}

function chooseSchool(s) {
  cr.school = s;
  cr.professionSkills = [s];
  cr.spells = [];
}

function setAge(id) {
  cr.age = id;
  if (cr.freeSkills.length > wanted.value) cr.freeSkills.splice(wanted.value);
}

const byName = (list, key, name) => list.find((x) => x[key].toLowerCase() === String(name).toLowerCase());

function rollKin() {
  const faces = catalog.value.tables.kin;
  const found = faces.length ? byName(catalog.value.kins, 'title', pick(faces)) : null;
  const k = found ?? pick(catalog.value.kins);
  chooseKin(k.key);
  note.value = `Rolled: ${k.title}`;
}

function rollProfession() {
  const faces = catalog.value.tables.profession;
  const found = faces.length ? byName(catalog.value.professions, 'title', pick(faces)) : null;
  const p = found ?? pick(catalog.value.professions);
  chooseProfession(p.key);
  note.value = `Rolled: ${p.title}`;
}

function rollAge() {
  const r = d(6);
  setAge(r <= 3 ? 'young' : r <= 5 ? 'adult' : 'old');
  note.value = `Rolled ${r}: ${ageInfo.value.label}`;
}

// what is typed is kept as typed: the limits are only checked to move on, and shown in red meanwhile
function setScore(i, raw) {
  cr.rolled[i] = Math.min(99, Math.max(0, Number.parseInt(raw, 10) || 0));
}
const scoreBad = (v) => v > 0 && (v < 3 || v > 18);

function rollAll() {
  cr.rolled = cr.rolled.map(() => rollAttribute());
  note.value = 'Rolled six attributes';
}

function swapScores() {
  [cr.rolled[swap.a], cr.rolled[swap.b]] = [cr.rolled[swap.b], cr.rolled[swap.a]];
}

function toggleProfessionSkill(s) {
  const at = cr.professionSkills.indexOf(s);
  if (at >= 0) cr.professionSkills.splice(at, 1);
  else {
    cr.professionSkills.push(s);
    cr.freeSkills = cr.freeSkills.filter((x) => x !== s);       // a skill of the profession cannot also be a free one
  }
}

function toggleFreeSkill(s) {
  const at = cr.freeSkills.indexOf(s);
  if (at >= 0) cr.freeSkills.splice(at, 1);
  else cr.freeSkills.push(s);
}

function chooseForMe() {
  let pool = [...options.value];
  cr.professionSkills = [];
  if (cr.school) {
    cr.professionSkills.push(cr.school);
    pool = pool.filter((s) => s !== cr.school);
  }
  while (cr.professionSkills.length < need.value && pool.length) cr.professionSkills.push(pool.splice(d(pool.length) - 1, 1)[0]);
  const free = freeChoices.value.map((s) => s.title).filter((t) => !cr.professionSkills.includes(t));
  cr.freeSkills = [];
  while (cr.freeSkills.length < wanted.value && free.length) cr.freeSkills.push(free.splice(d(free.length) - 1, 1)[0]);
}

function toggleSpell(key, tricks) {
  const at = cr.spells.indexOf(key);
  if (at >= 0) cr.spells.splice(at, 1);
  else if (spellCount(tricks) < (tricks ? prof.value.magic.tricks : prof.value.magic.spells)) cr.spells.push(key);
}

function chooseGearSet(i) {
  cr.gearCustom = false;
  cr.customGear = [];
  cr.gearSet = i;
  cr.gear = gearSets.value[i].picks.map((p) => ({ choice: 0, rolled: p.diceSides ? d(p.diceSides) : 0 }));
}

function rollGearSet() {
  const r = d(6);
  chooseGearSet(r <= 2 ? 0 : r <= 4 ? 1 : 2);
  note.value = `Rolled ${r}: set ${cr.gearSet + 1}`;
}

function rollField(field, table) {
  cr[field] = pick(table.faces);
}

// ---- the server -------------------------------------------------------------------------------------
async function randomize() {
  busy.value = true;
  try {
    Object.assign(cr, blank(), await apiSend('POST', '/gm/creation/random', props.token, {}));
    step.value = STEPS.length - 1;
    furthest.value = STEPS.length - 1;
    note.value = 'A random character. Change anything, or create it.';
  } catch (e) {
    note.value = e.message ?? 'Could not roll a character.';
  } finally {
    busy.value = false;
  }
}

// every list opens with its first option picked (the age starts as adult, the book's usual)
function pickDefaults() {
  if (!catalog.value || !prof.value) return;
  if (step.value === 1 && isMage.value && !cr.school) chooseSchool(prof.value.schools[0]);
  if (step.value === 5 && !isMage.value && !cr.heroicAbility && heroicList.value.length) cr.heroicAbility = heroicList.value[0].name;
  if (step.value === 6 && gearSets.value.length && cr.gearSet < 0 && !cr.gearCustom) chooseGearSet(0);
}
watch([step, () => cr.profession, () => cr.school], pickDefaults, { flush: 'sync' });

let timer = null;
watch([step, () => JSON.stringify(cr)], () => {
  clearTimeout(timer);
  if (!catalog.value || (step.value !== 3 && step.value !== STEPS.length - 1)) return;
  timer = setTimeout(async () => {
    try {
      preview.value = await apiSend('POST', '/gm/creation/preview', props.token, { ...cr });
    } catch { /* the review shows nothing until the server answers */ }
  }, 150);
});

async function create() {
  if (busy.value) return;
  busy.value = true;
  try {
    const made = await apiSend('POST', '/gm/characters', props.token, { creation: { ...cr } });
    emit('created', made.id);
  } catch (e) {
    note.value = e.message ?? 'Could not create the character.';
  } finally {
    busy.value = false;
  }
}

const short = (text, max = 110) => (text.length > max ? `${text.slice(0, max)}…` : text);
</script>

<template>
  <div class="creator" data-test="creator">
    <p v-if="loadError" class="error" style="padding: 1rem;">{{ loadError }}</p>
    <p v-else-if="!catalog" class="muted" style="padding: 1rem;">Loading…</p>
    <p v-else-if="!catalog.kins.length || !catalog.professions.length" class="muted" style="padding: 1rem;">
      There is no kin or profession loaded, so a character cannot be created. Check that the Core pack is in place.
    </p>
    <template v-else>
      <h2 class="title">New character</h2>
      <nav class="steps">
        <button
          v-for="(name, i) in STEPS" :key="name" type="button" class="pill"
          :class="{ current: step === i, done: reached(i) && i !== step && stepDone(i) }" :disabled="!reached(i)"
          data-test="creator-step" @click="goto(i)"
        >{{ i + 1 }}. {{ name }}</button>
      </nav>

      <section class="body">
        <div class="step-body">
          <h3>{{ STEPS[step] }}</h3>

          <!-- 1. Kin -->
          <template v-if="step === 0">
            <p>Choose your kin. Each has an innate ability that no other kin can learn.</p>
            <button type="button" class="btn" @click="rollKin">Roll D12</button>
            <ChoiceLayout :rows="kinRows" :model-value="cr.kin" :detail="detailOf(content.kin, kin)" @update:model-value="chooseKin" />
          </template>

          <!-- 2. Profession -->
          <template v-else-if="step === 1">
            <p>Your profession decides six of your trained skills, your heroic ability and what gear you start with.</p>
            <button type="button" class="btn" @click="rollProfession">Roll D10</button>
            <ChoiceLayout :rows="professionRows" :model-value="cr.profession" :detail="detailOf(content.professions, prof)" @update:model-value="chooseProfession">
              <div v-if="isMage" class="schools">
                <b class="gold">School of magic</b> <span class="muted">(becomes one of your trained skills)</span>
                <label v-for="s in prof.schools" :key="s" class="radio"><input type="radio" name="school" :checked="cr.school === s" data-test="school" @change="chooseSchool(s)" /> {{ s }}</label>
              </div>
            </ChoiceLayout>
          </template>

          <!-- 3. Age -->
          <template v-else-if="step === 2">
            <p>Older adventurers start with lower attributes but more trained skills. The modifications do not stack.</p>
            <button type="button" class="btn" @click="rollAge">Roll D6</button>
            <ChoiceLayout :rows="ageRows" :model-value="cr.age" :detail="ageDetail" @update:model-value="setAge" />
          </template>

          <!-- 4. Attributes -->
          <template v-else-if="step === 3">
            <p>Roll 4D6 and remove the worst die for each attribute, assigning each score as you roll it. When all six are set you may swap two. Then your age adjusts them (never above 18). Or enter the scores by hand if the group uses another method.</p>
            <button type="button" class="btn" data-test="roll-all" @click="rollAll">Roll all six</button>
            <button type="button" class="btn ghost" @click="cr.rolled = [0, 0, 0, 0, 0, 0]">Clear</button>
            <table class="attrs">
              <thead><tr><th>Attribute</th><th>Score</th><th></th><th>With age</th><th>Base chance</th><th></th></tr></thead>
              <tbody>
                <tr v-for="(a, i) in catalog.attributes" :key="a.short">
                  <td><b class="gold">{{ a.short }}</b> <span class="muted">{{ a.long }}</span></td>
                  <td><input type="number" :class="{ bad: scoreBad(cr.rolled[i]) }" :value="cr.rolled[i] || ''" :aria-label="`${a.long} score`" :aria-invalid="scoreBad(cr.rolled[i])" data-test="score" @input="setScore(i, $event.target.value)" /></td>
                  <td><button type="button" class="btn small" @click="cr.rolled[i] = rollAttribute()">Roll 4D6</button></td>
                  <td>{{ cr.rolled[i] ? finalAttrs[i] : '' }}</td>
                  <td>{{ cr.rolled[i] ? chanceOf(finalAttrs[i]) : '' }}</td>
                  <td class="muted">{{ prof?.keyAttribute === a.short ? 'key attribute of the profession' : '' }}</td>
                </tr>
              </tbody>
            </table>
            <p v-if="anyScoreBad" class="error" data-test="score-error">Scores go from 3 to 18.</p>
            <div v-if="allAttrsSet" class="swap">
              <b class="gold">Swap two scores</b>
              <select v-model.number="swap.a" aria-label="First attribute"><option v-for="(a, i) in catalog.attributes" :key="a.short" :value="i">{{ a.short }}</option></select>
              <select v-model.number="swap.b" aria-label="Second attribute"><option v-for="(a, i) in catalog.attributes" :key="a.short" :value="i">{{ a.short }}</option></select>
              <button type="button" class="btn small" @click="swapScores">Swap</button>
              <p v-if="preview?.derived" class="derived" data-test="derived">
                <b class="gold">Derived</b> HP {{ preview.derived.hp }} · WP {{ preview.derived.wp }} · Movement {{ preview.derived.movement ?? '?' }} ·
                Damage bonus STR {{ preview.derived.damageStr || '–' }} / AGL {{ preview.derived.damageAgl || '–' }} · Encumbrance limit {{ preview.derived.encumbrance }}
              </p>
            </div>
          </template>

          <!-- 5. Skills -->
          <template v-else-if="step === 4">
            <p>A trained skill starts at twice its base chance. Choose {{ need }} from your profession's list and {{ wanted }} of your own ({{ ageInfo?.label }}).</p>
            <button type="button" class="btn" data-test="choose-for-me" @click="chooseForMe">Choose for me</button>
            <div class="cols">
              <div>
                <b class="accent">From your profession: {{ cr.professionSkills.length }} of {{ need }}</b>
                <label v-for="s in options" :key="s" class="check">
                  <input type="checkbox" :checked="cr.professionSkills.includes(s)" :disabled="(isMage && s === cr.school) || (!cr.professionSkills.includes(s) && cr.professionSkills.length >= need)" data-test="prof-skill" @change="toggleProfessionSkill(s)" />
                  {{ s }} <span class="muted">(level {{ levelOf(s) }})</span>
                </label>
              </div>
              <div>
                <b class="accent">Your own: {{ cr.freeSkills.length }} of {{ wanted }}</b>
                <label v-for="s in freeChoices" :key="s.title" class="check" :title="s.summary">
                  <input type="checkbox" :checked="cr.professionSkills.includes(s.title) || cr.freeSkills.includes(s.title)" :disabled="cr.professionSkills.includes(s.title) || (!cr.freeSkills.includes(s.title) && cr.freeSkills.length >= wanted)" data-test="free-skill" @change="toggleFreeSkill(s.title)" />
                  {{ s.title }} <span class="muted">({{ s.attribute }}, level {{ levelOf(s.title) }})</span>
                </label>
              </div>
            </div>
          </template>

          <!-- 6. Ability & magic -->
          <template v-else-if="step === 5">
            <div v-if="kin?.innate.length">
              <b class="gold">Innate ability (from your kin)</b>
              <ul><li v-for="a in kin.innate" :key="a.name" :title="a.summary">{{ a.name }}<span v-if="a.wpCost" class="muted"> (WP {{ a.wpCost }})</span></li></ul>
            </div>
            <template v-if="isMage">
              <p>A mage gets no heroic ability at the start; the magic makes up for it. Choose {{ prof.magic.spells }} rank {{ prof.magic.rank }} spells and {{ prof.magic.tricks }} magic tricks from your school ({{ cr.school }}) or General Magic.</p>
              <div v-for="tricks in [false, true]" :key="tricks">
                <b class="accent">{{ tricks ? 'Magic tricks' : 'Spells' }}: {{ spellCount(tricks) }} of {{ tricks ? prof.magic.tricks : prof.magic.spells }}</b>
                <label v-for="s in magicChoices(tricks)" :key="s.key" class="check" :title="s.summary">
                  <input type="checkbox" :checked="cr.spells.includes(s.key)" :disabled="!cr.spells.includes(s.key) && spellCount(tricks) >= (tricks ? prof.magic.tricks : prof.magic.spells)" data-test="spell" @change="toggleSpell(s.key, tricks)" />
                  {{ s.title }} <span class="muted">({{ s.school }})</span>
                </label>
              </div>
            </template>
            <template v-else>
              <p>Your profession gives you a heroic ability. Requirements do not apply to your starting ability.</p>
              <ChoiceLayout :rows="heroicRows" :model-value="cr.heroicAbility" :detail="detailOf(content.abilities, null, cr.heroicAbility)" @update:model-value="(v) => (cr.heroicAbility = v)" />
              <p class="muted">Or, with the GM's permission, another heroic ability:</p>
              <select v-model="cr.heroicAbility" aria-label="Another heroic ability">
                <option value="">Choose…</option>
                <option v-for="h in catalog.heroicAbilities" :key="h.name" :value="h.name">{{ h.name }}</option>
              </select>
            </template>
          </template>

          <!-- 7. Gear -->
          <template v-else-if="step === 6">
            <p>Pick one of the starting sets of your profession, or roll for it. Dice in a set are rolled for you. Where a set offers a choice (A/B/C), pick one. Or choose Custom and put together whatever you want.</p>
            <button v-if="gearSets.length" type="button" class="btn" @click="rollGearSet">Roll D6</button>
            <ChoiceLayout :rows="gearRows" :model-value="gearChoice" :detail="gearDetail" @update:model-value="(v) => (v === 'custom' ? chooseCustomGear() : chooseGearSet(Number(v)))">
              <div v-if="cr.gearCustom" data-test="custom-gear">
                <div class="custom-filter">
                  <input v-model="customQuery" type="search" placeholder="Search…" aria-label="Search gear" data-test="custom-search" />
                  <select v-model="customKind" aria-label="Kind of gear">
                    <option value="all">Everything</option><option value="weapons">Weapons</option><option value="armor">Armor</option><option value="gear">Gear</option>
                  </select>
                </div>
                <div class="custom-choices">
                  <button v-for="i in customChoices" :key="i.key" type="button" class="custom-choice" data-test="custom-add" @click="addCustom(i.key)">
                    <span>{{ i.name }} <span class="muted">{{ i.sub }}</span></span><span class="muted">{{ priceText(i.copper) }} +</span>
                  </button>
                  <p v-if="!customChoices.length" class="muted" style="padding: 6px 10px;">Nothing found.</p>
                </div>
                <b class="accent">Your gear</b>
                <p v-if="!customList.length" class="muted">Nothing yet. Add things from the list above.</p>
                <div v-for="i in customList" :key="i.key" class="gear-row" data-test="custom-item">
                  <span class="grow">{{ i.name }}</span>
                  <button type="button" class="btn small" :aria-label="`One less ${i.name}`" @click="setCustomCount(i.key, i.count - 1)">−</button>
                  <span class="count">{{ i.count }}</span>
                  <button type="button" class="btn small" :aria-label="`One more ${i.name}`" @click="setCustomCount(i.key, i.count + 1)">+</button>
                  <span class="muted price">{{ priceText(i.copper === null ? null : i.copper * i.count) }}</span>
                </div>
                <p class="total" data-test="custom-total"><b class="gold">Total cost</b> {{ priceText(customTotal) }}<span v-if="customUnpriced" class="muted"> ({{ customUnpriced }} without a price not counted)</span></p>
              </div>
              <div v-else-if="cr.gearSet >= 0">
                <b class="accent">Your gear</b>
                <div v-for="(p, i) in gearSets[cr.gearSet].picks" :key="i" class="gear-row">
                  <template v-if="p.diceSides">
                    {{ cr.gear[i].rolled }} {{ p.what }} <span class="muted">(D{{ p.diceSides }})</span>
                    <button type="button" class="btn small" @click="cr.gear[i].rolled = d(p.diceSides)">Reroll</button>
                  </template>
                  <select v-else-if="p.options.length > 1" v-model.number="cr.gear[i].choice" :aria-label="`Choice ${i + 1}`">
                    <option v-for="(o, k) in p.options" :key="o" :value="k">{{ o }}</option>
                  </select>
                  <template v-else>• {{ p.options[0] }}</template>
                </div>
              </div>
            </ChoiceLayout>
          </template>

          <!-- 8. Name & details -->
          <template v-else-if="step === 7">
            <label class="field"><b class="gold">Name</b><input v-model="cr.name" placeholder="Name" data-test="name" /></label>
            <div v-if="kin?.names.length" class="chips">
              <span class="muted">Typical {{ kin.title }} names (click one or roll D6):</span>
              <button v-for="n in kin.names" :key="n" type="button" class="btn small" @click="cr.name = n">{{ n }}</button>
              <button type="button" class="btn small" @click="cr.name = pick(kin.names)">Roll</button>
            </div>
            <label class="field"><b class="gold">Nickname</b> <span class="muted">(optional)</span><input v-model="cr.nickname" placeholder="Nickname" /></label>
            <div v-if="prof?.nicknames.length" class="chips">
              <button v-for="n in prof.nicknames" :key="n" type="button" class="btn small" @click="cr.nickname = n">{{ n }}</button>
              <button type="button" class="btn small" @click="cr.nickname = pick(prof.nicknames)">Roll</button>
            </div>
            <label class="field"><b class="gold">Player</b> <span class="muted">(optional)</span><input v-model="cr.player" placeholder="Who plays this character" /></label>
            <template v-for="f in [['weakness', 'Weakness (optional)'], ['memento', 'Memento (optional)'], ['appearance', 'Appearance']]" :key="f[0]">
              <label class="field"><b class="gold">{{ f[1] }}</b><input v-model="cr[f[0]]" /></label>
              <div class="chips">
                <button v-for="t in catalog.tables[f[0]]" :key="t.title" type="button" class="btn small" @click="rollField(f[0], t)">Roll {{ t.dice }}{{ catalog.tables[f[0]].length > 1 ? ` · ${t.title}` : '' }}</button>
                <button type="button" class="btn small ghost" @click="cr[f[0]] = ''">Clear</button>
              </div>
            </template>
          </template>

          <!-- 9. Review -->
          <template v-else>
            <template v-if="preview?.problems.length">
              <p class="error">Not finished yet:</p>
              <ul><li v-for="p in preview.problems" :key="p" data-test="problem">{{ p }}</li></ul>
            </template>
            <template v-else-if="preview">
              <p class="muted">This is how the sheet will start. You can change anything on it afterwards.</p>
              <pre class="summary" data-test="summary">{{ preview.summary }}</pre>
            </template>
            <p v-else class="muted">Checking…</p>
          </template>
        </div>

        <footer class="foot">
          <button type="button" class="btn" :disabled="step === 0" data-test="creator-back" @click="step -= 1">&lt; Back</button>
          <button v-if="step < STEPS.length - 1" type="button" class="btn" :disabled="!stepDone(step)" :title="stepDone(step) ? '' : 'Finish this step first'" data-test="creator-next" @click="next">Next &gt;</button>
          <button v-else type="button" class="btn primary" :disabled="!preview || preview.problems.length > 0 || busy" data-test="creator-create" @click="create">Create character</button>
          <button type="button" class="btn" data-test="creator-random" :disabled="busy" @click="randomize">Random character</button>
          <button type="button" class="btn ghost" data-test="creator-cancel" @click="emit('cancel')">Cancel</button>
          <span class="muted note">{{ note }}</span>
        </footer>
      </section>
    </template>
  </div>
</template>

<style scoped>
.creator { display: flex; flex-direction: column; gap: 10px; padding: 12px 14px; height: 100%; box-sizing: border-box; min-height: 0; }
.steps { flex: none; display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
.title { margin: 0; text-align: center; font-size: 1.1rem; color: var(--green-dark); }
.pill { flex: none; background: var(--parchment); color: var(--muted); border: 1px solid var(--line); border-radius: 999px; padding: 5px 12px; font: inherit; white-space: nowrap; cursor: pointer; }
.pill:disabled { cursor: default; opacity: 0.6; }
.pill.done { color: var(--ink); }
.pill.current { color: #fff; background: var(--green); border-color: var(--green-dark); }
.body { flex: 1; min-width: 0; display: flex; flex-direction: column; }
.step-body { flex: 1; overflow-y: auto; padding: 4px 2px 12px; }
.step-body h3 { margin: 0 0 8px; font-size: 1.25rem; font-weight: 500; }
.foot { display: flex; align-items: center; gap: 8px; padding: 8px 12px; border-top: 1px solid var(--line); }
.note { margin-left: 12px; }
.btn { padding: 4px 14px; border: 1px solid var(--line); border-radius: 6px; background: #e3d3a1; color: var(--ink); font: inherit; cursor: pointer; }
.btn:disabled { opacity: 0.45; cursor: default; }
.btn.small { padding: 1px 8px; font-size: 0.8rem; }
.btn.ghost { background: none; }
.btn.primary { background: var(--green-dark); border-color: var(--green-dark); color: #f3ead2; font-weight: 600; }
.choice { display: flex; flex-direction: column; align-items: flex-start; gap: 2px; width: 100%; margin-top: 4px; padding: 6px 10px; border: 0; border-radius: 6px; background: none; font: inherit; color: var(--ink); text-align: left; cursor: pointer; }
.choice:hover { background: var(--parchment); }
.choice.on { background: #bcd9cf; }
.choice span { font-size: 0.85rem; }
.detail { margin-top: 12px; padding-top: 8px; border-top: 1px solid var(--line); }
.muted { color: var(--muted); }
.gold { color: var(--brown, #8a6d2f); }
.accent { color: var(--green-dark); }
.error { color: var(--danger, #c0392b); }
.fields th { text-align: left; padding-right: 12px; color: var(--muted); font-weight: 500; vertical-align: top; }
.attrs { border-collapse: collapse; margin-top: 8px; width: 100%; }
.attrs th, .attrs td { padding: 3px 12px 3px 0; text-align: left; }
.attrs input.bad { color: var(--danger, #c0392b); border-color: var(--danger, #c0392b); outline-color: var(--danger, #c0392b); }
.attrs input { width: 56px; font: inherit; padding: 2px 4px; }
.swap { margin-top: 12px; display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.derived { flex-basis: 100%; margin: 8px 0 0; }
.cols { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-top: 8px; }
.check, .radio { display: block; padding: 1px 0; }
.schools { margin-bottom: 8px; }
.radio { display: inline-block; margin-left: 12px; }
.custom-filter { display: flex; gap: 8px; margin: 6px 0; }
.custom-filter input { flex: 1; font: inherit; padding: 4px 8px; border: 1px solid var(--line); border-radius: 6px; background: #fbf6e6; color: var(--ink); }
.custom-choices { max-height: 220px; overflow-y: auto; border: 1px solid var(--line); border-radius: 6px; margin-bottom: 12px; background: #fbf6e6; }
.custom-choice { display: flex; justify-content: space-between; gap: 8px; width: 100%; padding: 5px 10px; border: 0; border-bottom: 1px solid var(--line); background: none; font: inherit; color: var(--ink); text-align: left; cursor: pointer; }
.custom-choice:hover { background: var(--parchment); }
.grow { flex: 1; }
.count { min-width: 1.5rem; text-align: center; }
.price { min-width: 6rem; text-align: right; }
.total { margin: 10px 0 0; }
.gear-row { padding: 3px 0; display: flex; gap: 8px; align-items: center; }
.field { display: flex; flex-direction: column; gap: 3px; margin-top: 10px; max-width: 340px; }
.field input, select { font: inherit; padding: 4px 8px; border: 1px solid var(--line); border-radius: 6px; background: #fbf6e6; color: var(--ink); }
.chips { display: flex; gap: 6px; flex-wrap: wrap; align-items: center; margin-top: 6px; }
.summary { white-space: pre-wrap; font: inherit; margin: 8px 0 0; }
@media (max-width: 700px) { .cols { grid-template-columns: 1fr; } }
</style>
