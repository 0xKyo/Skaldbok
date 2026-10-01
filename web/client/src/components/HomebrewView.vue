<script setup>
// The GM's homebrew: a pack of their own (named as they like, "Homebrew" by default) that holds what they make, and the Reference shows it like any
// other pack. A choice of what to make on top — creatures, or one of the kinds of data file of Core — and the editor of that kind below.
import { computed, onMounted, ref } from 'vue';
import { ApiError, apiGet, apiSend } from '../api.js';
import HomebrewCreatures from './HomebrewCreatures.vue';
import HomebrewEntries from './HomebrewEntries.vue';

const props = defineProps({
  token: { type: String, required: true },
});

const sections = ref([]);
const pack = ref({ id: 'homebrew', name: 'Homebrew' });
const kind = ref('creatures');
const nameDraft = ref('Homebrew');
const status = ref('');
const pending = ref(null);                // making a name for a field of another form: {from, key, form, openIndex, name}; after it we go back
const restore = ref(null);                // what the form we go back to is given: {form, openIndex, add}
const failed = ref(false);
const packs = ref([]);                    // the packs one can look into: [{id, name, editable}], the GM's own first, then Core and the others
const viewing = ref('');                  // the id of the pack being looked at; empty: the GM's own
const viewed = computed(() => packs.value.find((p) => p.id === viewing.value && !p.editable) ?? null);   // null: it is the GM's own

const kinds = computed(() => [...(viewed.value ? [] : [{ id: 'creatures', label: 'Creatures' }]), ...sections.value.map((s) => ({ id: s.id, label: s.label }))]);
const section = computed(() => sections.value.find((s) => s.id === kind.value) ?? null);

function take(schema) {
  sections.value = schema.sections ?? [];
  pack.value = schema.pack ?? pack.value;
  nameDraft.value = pack.value.name;
}

onMounted(async () => {
  try {
    take(await apiGet('/gm/homebrew', props.token));
  } catch {
    sections.value = [];                                   // the creatures editor still works
  }
  try {
    packs.value = (await apiGet('/gm/homebrew/packs', props.token)).packs ?? [];
  } catch {
    packs.value = [];
  }
});

// looking into another pack (Core, a tome of the user's): its entries are read only; a house rule or a copy goes to the GM's own pack
function look(id) {
  pending.value = null;
  restore.value = null;
  viewing.value = id === pack.value.id ? '' : id;
  if (viewing.value && kind.value === 'creatures') kind.value = sections.value[0]?.id ?? 'creatures';
}

// a kind may have just got a new choice (a duration made on its own tab is a choice of the spells), so the kinds are asked again
async function pick(id) {
  pending.value = null;
  restore.value = null;
  kind.value = id;
  try {
    const schema = await apiGet('/gm/homebrew', props.token);
    sections.value = schema.sections ?? sections.value;
  } catch {
    /* the kinds we have still work */
  }
}

// "+ New…" in a pick-list: go to that kind to make it; saving (or cancelling) comes back to the form that asked, as it was
const labelOf = (id) => sections.value.find((s) => s.id === id)?.label ?? id;
function makeFor(request) {
  pending.value = { ...request, from: kind.value };
  restore.value = null;
  kind.value = request.section;
}
function comeBack(name) {
  const p = pending.value;
  pending.value = null;
  restore.value = { form: p.form, openIndex: p.openIndex, add: name ? { key: p.key, name } : null };
  kind.value = p.from;
}
const prefill = computed(() => (pending.value?.from === 'kin' && pending.value.name ? { type: 'kin', kin: pending.value.name } : null));

async function rename() {
  failed.value = false;
  try {
    take(await apiSend('PUT', '/gm/homebrew/pack', props.token, { name: nameDraft.value }));
    viewing.value = '';
    try {
      packs.value = (await apiGet('/gm/homebrew/packs', props.token)).packs ?? packs.value;
    } catch {
      /* the list we have still works */
    }
    status.value = `What you make now goes in «${pack.value.name}».`;
  } catch (e) {
    failed.value = true;
    status.value = e instanceof ApiError ? e.message : 'Could not change the name.';
  }
}
</script>

<template>
  <section class="homebrew" data-test="homebrew">
    <form class="pack panel" data-test="pack-form" @submit.prevent="rename">
      <label>Pack <input v-model="nameDraft" maxlength="40" aria-label="Pack name" data-test="pack-name" /></label>
      <button type="submit" class="pill" :disabled="!nameDraft.trim() || nameDraft.trim() === pack.name" data-test="pack-save">Use this name</button>
      <span v-if="status" class="status" :class="{ error: failed }" data-test="pack-status">{{ status }}</span>
    </form>

    <label v-if="packs.length > 1" class="looking">Looking at
      <select :value="viewed ? viewed.id : pack.id" aria-label="Pack to look at" data-test="pack-view" @change="look($event.target.value)">
        <option v-for="p in packs" :key="p.id" :value="p.id">{{ p.name }}{{ p.editable ? ' (yours)' : ' (read only)' }}</option>
      </select>
    </label>
    <p v-if="viewed" class="looking-note panel" data-test="looking-note">
      🔒 You are looking into <b>{{ viewed.name }}</b>: it cannot be changed. Open an entry to make a <b>house rule</b> out of it (it replaces the original in the Rules, marked as changed by your pack) or to copy it.
      <button type="button" class="pill" data-test="back-to-mine" @click="look(pack.id)">Back to my pack</button>
    </p>

    <nav class="kinds" aria-label="What to make">
      <button v-for="k in kinds" :key="k.id" type="button" class="chip" :class="{ active: kind === k.id }" :data-test="`kind-${k.id}`" @click="pick(k.id)">{{ k.label }}</button>
    </nav>

    <p v-if="pending" class="making panel" data-test="making">
      Making a new entry for «{{ pending.name || labelOf(pending.from) }}» ({{ labelOf(pending.from) }}): save it and you come back to where you were.
      <button type="button" class="pill" data-test="making-cancel" @click="comeBack(null)">Cancel, go back</button>
    </p>
    <HomebrewCreatures v-if="kind === 'creatures'" :token="token" />
    <HomebrewEntries v-else-if="section" :key="section.id + (viewed ? '@' + viewed.id : '') + (pending ? '-making' : '') + (restore ? '-back' : '')" :token="token" :section="section" :restore="restore" :pack="viewed ? viewed.id : ''" :pack-name="viewed ? viewed.name : ''"
                     :start-new="!!pending" :prefill="prefill" @request-new="makeFor" @made="comeBack" />
  </section>
</template>

<style scoped>
.pack { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; padding: 8px 12px; margin-bottom: 10px; }
.pack label { display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: var(--muted); }
.pack input { font: inherit; color: var(--ink); padding: 4px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; }
.kinds { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 10px; }
.chip, .pill { padding: 4px 14px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.85rem; font-weight: 600; cursor: pointer; white-space: nowrap; }
.chip:hover, .pill:hover:not(:disabled) { background: var(--parchment); }
.chip.active { background: var(--green-dark); color: #f3ead2; }
.pill:disabled { opacity: 0.45; cursor: default; }
.looking { display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: var(--muted); margin-bottom: 10px; }
.looking select { font: inherit; color: var(--ink); padding: 4px 10px; border: 1px solid var(--line); border-radius: 8px; background: #fbf6e6; }
.looking-note { padding: 8px 12px; margin-bottom: 10px; font-size: 0.9rem; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; border-left: 3px solid var(--brown); }
.making { padding: 8px 12px; margin-bottom: 10px; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; font-size: 0.9rem; }
.status { font-size: 0.85rem; color: var(--green-dark); }
.status.error { color: var(--danger, #c0392b); }
</style>
