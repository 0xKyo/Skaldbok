import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import HomebrewView from './HomebrewView.vue';

let calls;
let spells;
let packName;

const schema = () => ({
  pack: { id: 'homebrew', name: packName },
  sections: [
    { id: 'spells', label: 'Spells', fields: [
      { key: 'name', label: 'Name', type: 'text' },
      { key: 'school', label: 'School', type: 'select', options: ['', 'Animism', 'Elementalism'] },
      { key: 'trick', label: 'Trick', type: 'bool' },
      { key: 'kind', label: 'Type', type: 'select', options: ['', 'kin', 'heroic'] },
      { key: 'kin', label: 'Kin', type: 'select', optionsFrom: 'kin', showWhen: { key: 'kind', value: 'kin' } },
      { key: 'requirement', label: 'Requirement', type: 'text', widget: 'requirement' },
      { key: 'range', label: 'Range', type: 'text', widget: 'range' },
      { key: 'prerequisite', label: 'Prerequisite', type: 'select', options: ['', 'Any School of Magic', 'Animism'], optionsFrom: 'spells', optionsUpper: true },
      { key: 'description', label: 'Description', type: 'long' },
    ] },
    { id: 'kin', label: 'Kin', fields: [
      { key: 'name', label: 'Name', type: 'text' },
      { key: 'innate_abilities', label: 'Innate abilities', type: 'list', optionsFrom: 'abilities' },
    ] },
    { id: 'abilities', label: 'Abilities', fields: [
      { key: 'name', label: 'Name', type: 'text' },
      { key: 'type', label: 'Type', type: 'select', options: ['', 'kin', 'heroic'] },
      { key: 'kin', label: 'Kin', type: 'select', optionsFrom: 'kin', showWhen: { key: 'type', value: 'kin' } },
    ] },
    { id: 'armor', label: 'Armor', fields: [
      { key: 'name', label: 'Name', type: 'text' },
      { key: 'banes', label: 'Banes', type: 'list' },
      { key: 'armor_bonuses', label: 'Armor bonuses', type: 'rows', cols: [{ key: 'damage_type', label: 'Damage type', type: 'text' }, { key: 'bonus', label: 'Bonus', type: 'number' }] },
    ] },
    { id: 'terrain', label: 'Terrain', fields: [
      { key: 'name', label: 'Name', type: 'text' },
      { key: 'fields', label: 'Columns', type: 'pairs' },
    ] },
  ],
});

beforeEach(() => {
  calls = [];
  packName = 'Homebrew';
  spells = [{ index: 0, name: 'Rime Lance', school: 'Elementalism', trick: true, description: 'A spear of ice.' }];
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    const [pathPart, queryPart] = url.replace('/api', '').split('?');
    const method = opts.method ?? 'GET';
    const body = opts.body ? JSON.parse(opts.body) : null;
    calls.push({ method, path: pathPart, query: queryPart, body });
    const ok = (data, status = 200) => ({ ok: true, status, json: async () => structuredClone(data) });
    if (method === 'GET' && pathPart === '/gm/homebrew') return ok(schema());
    if (method === 'GET' && pathPart === '/gm/homebrew/packs') return ok({ packs: [{ id: 'homebrew', name: packName, editable: true }, { id: 'core', name: 'Dragonbane Core', editable: false }] });
    if (method === 'GET' && pathPart === '/gm/homebrew/spells' && queryPart === 'pack=core') {
      return ok({ canReplace: true, entries: [{ index: 0, name: 'Birdsong', school: 'Animism', trick: true, key: 'core/spell/birdsong', houseRuled: false }, { index: 1, name: 'Clean', school: 'Animism', key: 'core/spell/clean', houseRuled: true }] });
    }
    if (method === 'GET' && pathPart === '/gm/homebrew/armor' && queryPart === 'pack=core') return ok({ canReplace: false, entries: [{ index: 0, name: 'Leather' }] });
    if (method === 'PUT' && pathPart === '/gm/homebrew/pack') { packName = body.name; return ok(schema()); }
    if (pathPart === '/gm/homebrew/spells' || pathPart.startsWith('/gm/homebrew/spells/')) {
      if (method === 'GET') return ok({ entries: spells });
      if (method === 'POST') { spells.push({ ...body, index: spells.length }); return ok({ ...body, index: spells.length - 1 }, 201); }
      if (method === 'PUT') { const i = Number(pathPart.split('/')[4]); spells[i] = { ...body, index: i }; return ok({ ...body, index: i }); }
      if (method === 'DELETE') { spells.splice(Number(pathPart.split('/')[4]), 1); return ok({ ok: true }); }
    }
    if (method === 'GET' && pathPart === '/gm/content/spells') return ok({ entries: [{ name: 'Farsight' }, { name: 'Pillar' }] });
    if (method === 'GET' && pathPart === '/gm/content/abilities') return ok({ entries: [{ name: 'Adaptive' }, { name: 'Hard to Catch' }] });
    if (method === 'GET' && pathPart === '/gm/content/kin') return ok({ entries: [{ name: 'Human' }, { name: 'Elf' }] });
    if (method === 'GET' && pathPart.startsWith('/gm/homebrew/')) return ok({ entries: [], creatures: [] });
    if (method === 'POST' && pathPart.startsWith('/gm/homebrew/')) return ok({ ...body, index: 0 }, 201);
    return { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
});
afterEach(() => vi.unstubAllGlobals());

const shown = (w, test) => w.get(`[data-test=${test}]`).element.style.display !== 'none';
const mountIt = async () => {
  const w = mount(HomebrewView, { props: { token: 'gm' } });
  await flushPromises();
  return w;
};

describe('HomebrewView', () => {
  it('offers creatures and every kind the server lists, creatures first', async () => {
    const w = await mountIt();
    expect(w.findAll('.chip').map((c) => c.text()).sort()).toEqual(['Abilities', 'Adventure', 'Armor', 'Creatures', 'Kin', 'Spells', 'Terrain']);
    expect(w.findAll('.chip')[0].text()).toBe('Creatures');
    expect(w.find('[data-test=creature-form]').exists()).toBe(false);
    expect(w.find('[data-test=new-creature]').exists()).toBe(true);
  });

  it('names the pack: Homebrew by default, changed with one button', async () => {
    const w = await mountIt();
    expect(w.get('[data-test=pack-name]').element.value).toBe('Homebrew');
    expect(w.get('[data-test=pack-save]').attributes('disabled')).toBeDefined();
    await w.get('[data-test=pack-name]').setValue('Frozen North');
    await w.get('[data-test=pack-form]').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'PUT' && c.path === '/gm/homebrew/pack').body).toEqual({ name: 'Frozen North' });
    expect(w.get('[data-test=pack-status]').text()).toContain('Frozen North');
  });

  it('the kinds come in groups: only the ones of the open group are shown, and a click on a group opens its first kind', async () => {
    const w = await mountIt();
    const groups = w.findAll('[role=tab]').map((b) => b.attributes('data-test'));
    expect(groups.length).toBeGreaterThanOrEqual(5);
    expect(groups[0]).toBe('group-creatures');
    expect(w.get('[data-test=group-creatures]').attributes('aria-selected')).toBe('true');
    expect(shown(w, 'kind-creatures')).toBe(true);
    expect(shown(w, 'kind-spells')).toBe(false);
    await w.get('[data-test=group-magic]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=group-magic]').attributes('aria-selected')).toBe('true');
    expect(shown(w, 'kind-spells')).toBe(true);
    expect(w.get('[data-test=kind-spells]').classes()).toContain('active');
    expect(shown(w, 'kind-creatures')).toBe(false);
  });

  it('lists the entries of a kind and edits one: the PUT names the entry it replaces', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    expect(w.findAll('[data-test=entry-row]').map((r) => r.text())).toEqual(['Rime Lance']);
    await w.get('[data-test=entry-row]').trigger('click');
    expect(w.get('[data-test=f-name]').element.value).toBe('Rime Lance');
    expect(w.get('[data-test=f-school]').element.value).toBe('Elementalism');
    expect(w.get('[data-test=f-trick]').element.checked).toBe(true);
    await w.get('[data-test=f-name]').setValue('Rime Spear');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    const put = calls.find((c) => c.method === 'PUT' && c.path === '/gm/homebrew/spells/0');
    expect(put.query).toBe('name=Rime%20Lance');
    expect(put.body.name).toBe('Rime Spear');
    expect(w.findAll('[data-test=entry-row]').map((r) => r.text())).toEqual(['Rime Spear']);
  });

  it('makes a new entry; the save needs a name', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    await w.get('[data-test=new-entry]').trigger('click');
    expect(w.get('[data-test=save-entry]').attributes('disabled')).toBeDefined();
    await w.get('[data-test=f-name]').setValue('Snow Veil');
    await w.get('[data-test=f-school]').setValue('Animism');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'POST' && c.path === '/gm/homebrew/spells').body).toMatchObject({ name: 'Snow Veil', school: 'Animism', trick: false });
    expect(w.findAll('[data-test=entry-row]')).toHaveLength(2);
  });

  it('a field that depends on another is shown only then, and its choices are the kins of the Reference', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    await w.get('[data-test=new-entry]').trigger('click');
    expect(w.find('[data-test=f-kin]').exists()).toBe(false);
    await w.get('[data-test=f-kind]').setValue('kin');
    expect(w.get('[data-test=f-kin]').findAll('option').map((o) => o.element.value)).toEqual(['', 'Human', 'Elf']);
    await w.get('[data-test=f-kin]').setValue('Elf');
    await w.get('[data-test=f-name]').setValue('Frost Sight');
    await w.get('[data-test=f-kind]').setValue('heroic');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'POST' && c.path === '/gm/homebrew/spells').body).not.toHaveProperty('kin');   // hidden, so not sent
  });

  it('the requirement of a spell, its range and its prerequisite are chosen, and what is sent is the text the data uses', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    await w.get('[data-test=new-entry]').trigger('click');
    await w.get('[data-test=f-name]').setValue('Snow Veil');
    await w.get('[data-test=req-word]').setValue(true);
    await w.get('[data-test=req-material]').setValue(true);
    await w.get('[data-test=req-detail]').setValue('snow');
    await w.get('[data-test=range-kind]').setValue('Meters');
    await w.get('[data-test=range-meters]').setValue('10');
    await w.get('[data-test=range-shape]').setValue('sphere');
    expect(w.get('[data-test=f-prerequisite]').findAll('option').map((o) => o.element.value)).toEqual(['', 'Any School of Magic', 'Animism', 'FARSIGHT', 'PILLAR']);
    await w.get('[data-test=f-prerequisite]').setValue('PILLAR');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'POST' && c.path === '/gm/homebrew/spells').body).toMatchObject({ requirement: 'Word, material (snow)', range: '10 meters (sphere)', prerequisite: 'PILLAR' });
  });

  it('opens what the data wrote ("Word, gesture, ingredient (water)", "Touch") in the same controls', async () => {
    spells = [{ index: 0, name: 'Old', requirement: 'Word, gesture, ingredient (water)', range: 'Touch' }];
    const w = await mountIt();
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    await w.get('[data-test=entry-row]').trigger('click');
    expect(w.get('[data-test=req-word]').element.checked).toBe(true);
    expect(w.get('[data-test=req-gesture]').element.checked).toBe(true);
    expect(w.get('[data-test=req-detail]').element.value).toBe('water');
    expect(w.get('[data-test=range-kind]').element.value).toBe('Touch');
  });

  it('a list of abilities is picked, and a new ability is made on the Abilities tab and comes back to the kin as it was', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-kin]').trigger('click');
    await flushPromises();
    await w.get('[data-test=new-entry]').trigger('click');
    await w.get('[data-test=f-name]').setValue('Troll');
    await w.get('[data-test=pick-add]').setValue('Adaptive');
    await w.get('[data-test=pick-add]').trigger('change');
    expect(w.findAll('[data-test=picked]').map((p) => p.text().replace('×', '').trim())).toEqual(['Adaptive']);
    await w.get('[data-test=pick-new]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=making]').text()).toContain('Troll');
    expect(w.get('[data-test=f-name]').element.value).toBe('');                      // a new ability, of the kin
    expect(w.get('[data-test=f-type]').element.value).toBe('kin');
    await w.get('[data-test=f-name]').setValue('Thick Skin');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    expect(w.find('[data-test=making]').exists()).toBe(false);
    expect(w.get('[data-test=f-name]').element.value).toBe('Troll');
    expect(w.findAll('[data-test=picked]').map((p) => p.text().replace('×', '').trim())).toEqual(['Adaptive', 'Thick Skin']);
  });

  it('cancelling the new ability goes back to the kin with nothing added', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-kin]').trigger('click');
    await flushPromises();
    await w.get('[data-test=new-entry]').trigger('click');
    await w.get('[data-test=f-name]').setValue('Troll');
    await w.get('[data-test=pick-new]').trigger('click');
    await flushPromises();
    await w.get('[data-test=making-cancel]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=f-name]').element.value).toBe('Troll');
    expect(w.findAll('[data-test=picked]')).toHaveLength(0);
  });

  it('looks into Core: read only, and an entry can be house-ruled or copied into your own pack', async () => {
    const w = await mountIt();
    expect(w.findAll('[data-test=pack-view] option').map((o) => o.text())).toEqual(['Homebrew (yours)', 'Dragonbane Core (read only)']);
    await w.get('[data-test=pack-view]').setValue('core');
    await flushPromises();
    expect(w.get('[data-test=looking-note]').text()).toContain('Dragonbane Core');
    expect(w.find('[data-test=kind-creatures]').exists()).toBe(false);                      // creatures of the books are copied from the creatures tab of your own pack
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    expect(w.find('[data-test=new-entry]').exists()).toBe(false);
    expect(w.get('[data-test=read-only]').text()).toContain('read only');
    expect(w.findAll('[data-test=entry-row]').map((r) => r.text())).toEqual(['Birdsong', 'Cleanhouse-ruled']);
    await w.findAll('[data-test=entry-row]')[0].trigger('click');
    expect(w.get('[data-test=view-note]').exists()).toBe(true);
    expect(w.get('[data-test=entry-form] fieldset').attributes('disabled')).toBeDefined();
    expect(w.find('[data-test=save-entry]').exists()).toBe(false);
    await w.get('[data-test=house-rule-it]').trigger('click');
    expect(w.get('[data-test=house-note]').text()).toContain('Birdsong');
    await w.get('[data-test=f-description]').setValue('Louder.');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    const sent = calls.find((c) => c.method === 'POST' && c.path === '/gm/homebrew/spells');
    expect(sent.body.houseRuleOf).toEqual({ pack: 'core', name: 'Birdsong' });
    expect(sent.body.description).toBe('Louder.');
    expect(w.get('[data-test=list-status]').text()).toContain('house rule');
  });

  it('an entry already house-ruled offers no second rule; a kind that is not a card can only be copied', async () => {
    const w = await mountIt();
    await w.get('[data-test=pack-view]').setValue('core');
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    await w.findAll('[data-test=entry-row]')[1].trigger('click');
    expect(w.find('[data-test=house-rule-it]').exists()).toBe(false);
    expect(w.get('[data-test=view-note]').text()).toContain('already have a house rule');
    await w.get('[data-test=kind-armor]').trigger('click');
    await flushPromises();
    await w.get('[data-test=entry-row]').trigger('click');
    expect(w.find('[data-test=house-rule-it]').exists()).toBe(false);
    await w.get('[data-test=copy-it]').trigger('click');
    expect(w.get('[data-test=f-name]').element.value).toBe('Leather (copy)');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'POST' && c.path === '/gm/homebrew/armor').body).not.toHaveProperty('houseRuleOf');
  });

  it('what is made in your own pack that replaces something says so', async () => {
    spells = [{ index: 0, name: 'Birdsong', replaces: 'core/spell/birdsong' }];
    const w = await mountIt();
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=house-rule-mark]').text()).toBe('house rule');
    await w.get('[data-test=entry-row]').trigger('click');
    expect(w.get('[data-test=replaces-note]').text()).toContain('core/spell/birdsong');
  });

  it('lists, rows and columns are edited in place and sent clean', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-armor]').trigger('click');
    await flushPromises();
    await w.get('[data-test=new-entry]').trigger('click');
    await w.get('[data-test=f-name]').setValue('Fur Coat');
    await w.get('[data-test=f-banes]').setValue('Sneaking\n\n Swimming ');
    await w.get('[data-test=add-row]').trigger('click');
    await w.get('[data-test=add-row]').trigger('click');                                    // an empty row is dropped
    const inputs = w.findAll('[data-test=row-row]')[0].findAll('input');
    await inputs[0].setValue('Cold');
    await inputs[1].setValue('2');
    await w.get('[data-test=entry-form]').trigger('submit');
    await flushPromises();
    const sent = calls.find((c) => c.method === 'POST' && c.path === '/gm/homebrew/armor');
    expect(sent.body).toEqual({ name: 'Fur Coat', banes: ['Sneaking', 'Swimming'], armor_bonuses: [{ damage_type: 'Cold', bonus: '2' }] });
  });

  it('table entries have columns of label and value', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-terrain]').trigger('click');
    await flushPromises();
    await w.get('[data-test=new-entry]').trigger('click');
    await w.get('[data-test=add-pair]').trigger('click');
    const pair = w.get('[data-test=pair-row]').findAll('input');
    await pair[0].setValue('EFFECT');
    await pair[1].setValue('Bane on ranged attacks');
    expect(w.findAll('[data-test=pair-row]')).toHaveLength(1);
  });

  it('deleting asks first and names the entry', async () => {
    const w = await mountIt();
    await w.get('[data-test=kind-spells]').trigger('click');
    await flushPromises();
    await w.get('[data-test=entry-row]').trigger('click');
    await w.get('[data-test=delete-entry]').trigger('click');
    expect(w.get('[data-test=delete-confirm]').text()).toContain('Rime Lance');
    expect(calls.some((c) => c.method === 'DELETE')).toBe(false);
    await w.get('[data-test=delete-yes]').trigger('click');
    await flushPromises();
    const del = calls.find((c) => c.method === 'DELETE');
    expect(del.path).toBe('/gm/homebrew/spells/0');
    expect(del.query).toBe('name=Rime%20Lance');
    expect(w.findAll('[data-test=entry-row]')).toHaveLength(0);
  });

  it('still works as the creatures editor when the server has no schema', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false, status: 404, json: async () => ({ error: 'no' }) })));
    const w = await mountIt();
    expect(w.findAll('.chip').map((c) => c.text())).toEqual(['Creatures', 'Adventure']);
  });
});
