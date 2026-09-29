import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import CharacterCreator from './CharacterCreator.vue';

const catalog = {
  kins: [
    { key: 'core/kin/human', title: 'Human', movement: '10', innate: [{ name: 'Adaptive', wpCost: '', summary: '', fields: [], body: '' }], names: ['Aria', 'Bo'], body: 'Humans.\nMore.', source: '' },
    { key: 'core/kin/elf', title: 'Elf', movement: '10', innate: [], names: ['Elu'], body: 'Elves.', source: '' },
  ],
  professions: [
    {
      key: 'core/profession/bard', title: 'Bard', keyAttribute: 'CHA', schools: [], nicknames: ['the Loud'], body: 'Sings.', fields: [],
      skills: ['Acrobatics', 'Bluffing', 'Languages', 'Myths & Legends', 'Performance', 'Persuasion', 'Sleight of Hand'],
      skillsBySchool: {}, heroic: [{ name: 'Musician', summary: 'Plays.', fields: [], body: '' }],
      magic: { spells: 0, tricks: 0, rank: 0 },
      gearSets: [
        { text: 'lute, D6 silver', picks: [{ options: ['lute'], diceSides: 0, what: '' }, { options: ['D6 silver'], diceSides: 6, what: 'silver' }] },
        { text: 'a/b', picks: [{ options: ['a', 'b'], diceSides: 0, what: '' }] },
      ],
      source: '',
    },
  ],
  ages: [
    { id: 'young', label: 'Young', summary: 'y', trainedSkills: 8, attrMod: [0, 1, 1, 0, 0, 0] },
    { id: 'adult', label: 'Adult', summary: 'a', trainedSkills: 10, attrMod: [0, 0, 0, 0, 0, 0] },
    { id: 'old', label: 'Old', summary: 'o', trainedSkills: 12, attrMod: [-1, -1, -1, 0, 0, 0] },
  ],
  attributes: ['STR', 'CON', 'AGL', 'INT', 'WIL', 'CHA'].map((short) => ({ short, long: short })),
  baseChance: [0, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7],
  professionSkills: 6,
  skillAttributes: { Acrobatics: 'AGL', Bluffing: 'CHA', Axes: 'STR', Awareness: 'INT', Bushcraft: 'INT', Swimming: 'AGL' },
  selectableSkills: [['Axes', 'STR'], ['Awareness', 'INT'], ['Bushcraft', 'INT'], ['Swimming', 'AGL']].map(([title, attribute]) => ({ title, attribute, summary: '' })),
  heroicAbilities: [{ name: 'Musician', summary: '', fields: [], body: '' }],
  spells: [],
  tables: { kin: [], profession: [], weakness: [], memento: [], appearance: [{ title: 'Appearance', dice: 'D6', faces: ['Scarred'] }] },
};

let calls;
beforeEach(() => {
  calls = [];
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    const path = url.replace('/api', '');
    const method = opts.method ?? 'GET';
    const body = opts.body ? JSON.parse(opts.body) : null;
    calls.push({ method, path, body });
    const ok = (data, status = 200) => ({ ok: true, status, json: async () => structuredClone(data) });
    if (path === '/gm/creation') return ok(catalog);
    if (path === '/gm/content/kin') return ok({ entries: [{ key: 'core/kin/human', name: 'Human', pack: 'Dragonbane Core', fields: [{ label: 'Movement', value: '10' }], body: 'Humans are the last-born.', tables: [] }] });
    if (path === '/gm/content/weapons') return ok({ entries: [{ key: 'core/weapon/sword', name: 'Short sword', subtitle: 'Melee weapon', fields: [{ label: 'Cost', value: '5 gold' }] }] });
    if (path === '/gm/content/gear') return ok({ entries: [{ key: 'core/gear/rope', name: 'Rope', subtitle: 'Gear', fields: [{ label: 'Cost', value: '5 silver' }] }, { key: 'core/gear/lodging', name: 'Lodging', subtitle: 'Service', fields: [{ label: 'Cost', value: '2 gold/ day' }] }] });
    if (path.startsWith('/gm/content/')) return ok({ entries: [] });
    if (path === '/gm/creation/preview') {
      return ok({
        problems: body.name ? [] : ['Give the character a name.'],
        attributes: [10, 10, 10, 10, 10, 10],
        derived: { hp: 10, wp: 10, movement: 10, damageStr: '', damageAgl: '', encumbrance: 5 },
        summary: `Sheet of ${body.name}`,
      });
    }
    if (path === '/gm/creation/random') {
      return ok({ kin: 'core/kin/elf', profession: 'core/profession/bard', age: 'adult', school: '', rolled: [10, 11, 12, 13, 14, 15], professionSkills: [], freeSkills: [], heroicAbility: 'Musician', spells: [], gearSet: -1, gear: [], name: 'Elu' });
    }
    if (path === '/gm/characters' && method === 'POST') return ok({ id: 'c9' }, 201);
    return { ok: false, status: 404, json: async () => ({ error: 'nope' }) };
  }));
});
afterEach(() => vi.unstubAllGlobals());

const settle = async () => {
  await new Promise((r) => setTimeout(r, 200));      // the preview is debounced
  await flushPromises();
};
const mountIt = async (props = {}) => {
  const w = mount(CharacterCreator, { props: { token: 't', ...props } });
  await flushPromises();
  return w;
};
const nextBtn = (w) => w.get('[data-test=creator-next]');

describe('CharacterCreator', () => {
  it('lists the nine steps and locks the ones not reached yet', async () => {
    const w = await mountIt();
    expect(w.findAll('[data-test=creator-step]')).toHaveLength(9);
    expect(w.findAll('[data-test=creator-step]')[3].attributes('disabled')).toBeDefined();    // later steps are not reachable yet
  });

  it('shows the data of the chosen option beside the list, like the Reference', async () => {
    const w = await mountIt();
    expect(w.findAll('[data-test=choice-row]').map((r) => r.text())).toEqual(['Human', 'Elf']);
    expect(w.findAll('[data-test=choice-row]')[0].classes()).toContain('active');      // the first one is picked from the start
    const detail = w.get('[data-test=choice-detail]');
    expect(detail.text()).toContain('Human');
    expect(detail.text()).toContain('Source · Dragonbane Core');
    expect(detail.text()).toContain('Movement');
    expect(detail.text()).toContain('Humans are the last-born.');
    await w.findAll('[data-test=choice-row]')[1].trigger('click');      // an option without a Reference card shows what the catalog knows
    expect(w.get('[data-test=choice-detail]').text()).toContain('Elves.');
  });

  it('walks every step and sends the choices to create the character', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=choice-row]')[0].trigger('click');
    await nextBtn(w).trigger('click');
    await w.findAll('[data-test=choice-row]')[0].trigger('click');
    await nextBtn(w).trigger('click');
    await w.findAll('[data-test=choice-row]')[1].trigger('click');
    await nextBtn(w).trigger('click');
    await w.get('[data-test=roll-all]').trigger('click');
    await nextBtn(w).trigger('click');
    await w.get('[data-test=choose-for-me]').trigger('click');          // adult: 6 from the profession + 4 of its own
    expect(w.findAll('[data-test=prof-skill]').filter((i) => i.element.checked)).toHaveLength(6);
    expect(w.findAll('[data-test=free-skill]').filter((i) => i.element.checked)).toHaveLength(4);
    await nextBtn(w).trigger('click');
    await w.get('[data-test=choice-row]').trigger('click');
    await nextBtn(w).trigger('click');
    await w.findAll('[data-test=choice-row]')[0].trigger('click');
    await nextBtn(w).trigger('click');
    expect(nextBtn(w).attributes('disabled')).toBeDefined();            // a name is needed
    await w.get('[data-test=name]').setValue('Aria');
    await nextBtn(w).trigger('click');
    await settle();
    expect(w.get('[data-test=summary]').text()).toBe('Sheet of Aria');
    await w.get('[data-test=creator-create]').trigger('click');
    await flushPromises();
    const sent = calls.find((c) => c.method === 'POST' && c.path === '/gm/characters').body.creation;
    expect(sent).toMatchObject({ kin: 'core/kin/human', profession: 'core/profession/bard', age: 'adult', heroicAbility: 'Musician', gearSet: 0, name: 'Aria' });
    expect(sent.rolled.every((v) => v >= 3 && v <= 18)).toBe(true);
    expect(sent.gear).toHaveLength(2);
    expect(sent.gear[1].rolled).toBeGreaterThanOrEqual(1);
    expect(w.emitted('created')[0]).toEqual(['c9']);
  });

  it('Random character fills everything in and lands on the review', async () => {
    const w = await mountIt();
    await w.get('[data-test=creator-random]').trigger('click');
    await settle();
    expect(calls.some((c) => c.path === '/gm/creation/random')).toBe(true);
    expect(w.get('[data-test=summary]').text()).toBe('Sheet of Elu');
    expect(w.get('[data-test=creator-create]').attributes('disabled')).toBeUndefined();
  });

  it('opens already random when asked to', async () => {
    const w = await mountIt({ random: true });
    await settle();
    expect(w.find('[data-test=summary]').exists()).toBe(true);
  });

  it('picks the first option of every list by default', async () => {
    const w = await mountIt();
    const activeRow = () => w.findAll('[data-test=choice-row]').findIndex((r) => r.classes().includes('active'));
    await nextBtn(w).trigger('click');
    expect(activeRow()).toBe(0);                                          // profession
    await nextBtn(w).trigger('click');
    expect(w.findAll('[data-test=choice-row]')[1].classes()).toContain('active');   // age: adult, the book's usual
    await nextBtn(w).trigger('click');
    await w.get('[data-test=roll-all]').trigger('click');
    await nextBtn(w).trigger('click');
    await w.get('[data-test=choose-for-me]').trigger('click');
    await nextBtn(w).trigger('click');
    expect(activeRow()).toBe(0);                                          // heroic ability
    await nextBtn(w).trigger('click');
    expect(activeRow()).toBe(0);                                          // gear set
    expect(nextBtn(w).attributes('disabled')).toBeUndefined();
  });

  it('Custom gear: pick anything from the rules and see what it all costs', async () => {
    const w = await mountIt();
    for (let i = 0; i < 3; i += 1) await nextBtn(w).trigger('click');      // kin, profession, age
    await w.get('[data-test=roll-all]').trigger('click');
    await nextBtn(w).trigger('click');
    await w.get('[data-test=choose-for-me]').trigger('click');
    await nextBtn(w).trigger('click');
    await nextBtn(w).trigger('click');                                     // heroic ability (first one is picked)
    const rows = w.findAll('[data-test=choice-row]');
    expect(rows.map((r) => r.text())).toEqual(['Set 1', 'Set 2', 'Custom']);
    await rows[2].trigger('click');
    expect(w.get('[data-test=custom-total]').text()).toContain('0');
    const add = w.findAll('[data-test=custom-add]');
    expect(add).toHaveLength(3);
    await add[0].trigger('click');                                         // short sword, 5 gold
    await add[0].trigger('click');                                         // two of them
    await add[1].trigger('click');                                         // rope, 5 silver
    await add[2].trigger('click');                                         // lodging: no plain price
    expect(w.get('[data-test=custom-total]').text()).toContain('10 gold, 5 silver');
    expect(w.get('[data-test=custom-total]').text()).toContain('1 without a price');
    expect(w.findAll('[data-test=custom-item]')).toHaveLength(3);
    await w.get('[data-test=custom-search]').setValue('rope');
    expect(w.findAll('[data-test=custom-add]')).toHaveLength(1);
    expect(nextBtn(w).attributes('disabled')).toBeUndefined();
    await nextBtn(w).trigger('click');
    await w.get('[data-test=name]').setValue('Aria');
    await nextBtn(w).trigger('click');
    await new Promise((r) => setTimeout(r, 200));
    await flushPromises();
    await w.get('[data-test=creator-create]').trigger('click');
    await flushPromises();
    const sent = calls.find((c) => c.method === 'POST' && c.path === '/gm/characters').body.creation;
    expect(sent.gearSet).toBe(-1);
    expect(sent.customGear).toEqual([{ key: 'core/weapon/sword', count: 2 }, { key: 'core/gear/rope', count: 1 }, { key: 'core/gear/lodging', count: 1 }]);
  });

  it('keeps what is typed as a score and only flags it: red when out of range, no Next until 3 to 18', async () => {
    const w = await mountIt();
    for (let i = 0; i < 3; i += 1) await nextBtn(w).trigger('click');      // kin, profession, age
    await w.get('[data-test=roll-all]').trigger('click');
    const first = w.findAll('[data-test=score]')[0];
    await first.setValue('1');                                             // on the way to typing 16
    expect(first.element.value).toBe('1');                                 // not raised to 3
    expect(first.classes()).toContain('bad');
    expect(w.find('[data-test=score-error]').exists()).toBe(true);
    expect(nextBtn(w).attributes('disabled')).toBeDefined();
    await first.setValue('16');
    expect(first.element.value).toBe('16');
    expect(first.classes()).not.toContain('bad');
    expect(nextBtn(w).attributes('disabled')).toBeUndefined();
    await first.setValue('19');
    expect(first.classes()).toContain('bad');
    expect(nextBtn(w).attributes('disabled')).toBeDefined();
  });

  it('cancel says so', async () => {
    const w = await mountIt();
    await w.get('[data-test=creator-cancel]').trigger('click');
    expect(w.emitted('cancel')).toHaveLength(1);
  });
});
