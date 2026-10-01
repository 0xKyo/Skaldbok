import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import HomebrewCreatures from './HomebrewCreatures.vue';

let mine;
let calls;

const bookCreature = {
  id: 7, key: 'core/monster/wolf', name: 'Wolf', kind: 'animal', category: 'Wild animals', description: 'A grey hunter.', quote: '', randomEncounter: 'A pack at dusk.',
  adventureSeed: '', statsRef: '', pack: 'Dragonbane Core', image: null,
  blocks: [{ variant: '', fields: [{ label: 'Ferocity', value: '2' }, { label: 'HP', value: '9' }] }],
  attacks: [{ rollMin: 1, rollMax: 2, rollText: '1-2', name: 'Bite', text: 'Bite D8 damage.' }],
  abilities: [{ name: 'Keen nose', kind: 'ability', text: 'Tracks by smell.' }], tables: [],
};

beforeEach(() => {
  vi.useFakeTimers();
  calls = [];
  mine = [{ id: 'frost-wight', key: 'custom/monster/frost-wight', name: 'Frost Wight', kind: 'monster', category: 'Undead', description: 'Cold.', attack_dice: 'D8',
            statblocks: [{ fields: { Ferocity: '2', HP: '18' } }], attacks: [{ roll: '1-2', name: 'Claws', text: 'Rakes.' }], abilities: [{ name: 'Undead', text: 'Immune to poison.' }] }];
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    const path = url.replace('/api', '').split('?')[0];
    const method = opts.method ?? 'GET';
    const body = opts.body ? JSON.parse(opts.body) : null;
    calls.push({ method, path, body });
    const ok = (data, status = 200) => ({ ok: true, status, json: async () => structuredClone(data) });
    if (method === 'GET' && path === '/gm/homebrew/creatures') return ok({ creatures: mine });
    if (method === 'POST' && path === '/gm/homebrew/creatures') { const c = { ...body, id: 'new-one', key: 'custom/monster/new-one' }; mine.push(c); return ok(c, 201); }
    if (method === 'PUT' && path.startsWith('/gm/homebrew/creatures/')) { const id = path.split('/')[4]; const c = { ...body, id }; mine = mine.map((x) => (x.id === id ? c : x)); return ok(c); }
    if (method === 'DELETE' && path.startsWith('/gm/homebrew/creatures/')) { mine = mine.filter((x) => x.id !== path.split('/')[4]); return ok({ ok: true }); }
    if (method === 'GET' && path === '/gm/creatures') return ok({ creatures: [{ id: 7, key: 'core/monster/wolf', name: 'Wolf', sub: 'Wild animals', kind: 'animal' }] });
    if (method === 'GET' && path === '/gm/creatures/core%2Fmonster%2Fwolf') return ok(bookCreature);
    return { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
});
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

const mountIt = async () => {
  const w = mount(HomebrewCreatures, { props: { token: 'gm' } });
  await flushPromises();
  return w;
};

describe('HomebrewCreatures', () => {
  it('lists the GM\'s own creatures and opens one in the form', async () => {
    const w = await mountIt();
    expect(w.findAll('[data-test=creature-row]').map((r) => r.text())).toEqual(['Frost WightUndead']);
    await w.get('[data-test=creature-row]').trigger('click');
    expect(w.get('[data-test=f-name]').element.value).toBe('Frost Wight');
    expect(w.get('[data-test=f-kind]').element.value).toBe('monster');
    expect(w.get('[data-test=f-dice]').element.value).toBe('D8');
    expect(w.findAll('[data-test=stat-field]')).toHaveLength(2);
    expect(w.findAll('[data-test=attack-row]')).toHaveLength(1);
    expect(w.findAll('[data-test=ability-row]')).toHaveLength(1);
  });

  it('makes a new creature: a stat block to fill in, rows to add, and what is sent is clean', async () => {
    const w = await mountIt();
    await w.get('[data-test=new-creature]').trigger('click');
    expect(w.get('[data-test=save-creature]').attributes('disabled')).toBeDefined();          // a name is needed
    expect(w.findAll('[data-test=stat-field]')).toHaveLength(5);                              // Ferocity, Size, Movement, Armor, HP
    await w.get('[data-test=f-name]').setValue('  Bog Hag ');
    await w.get('[data-test=f-kind]').setValue('npc');
    await w.get('[data-test=f-category]').setValue('Hags');
    await w.findAll('[data-test=stat-field] input')[1].setValue('3');                          // Ferocity = 3
    await w.get('[data-test=add-attack]').trigger('click');
    await w.get('[data-test=add-attack]').trigger('click');                                    // an empty row is dropped when sending
    await w.findAll('[data-test=attack-row] input')[0].setValue('1-3');
    await w.findAll('[data-test=attack-row] input')[1].setValue('Grasp');
    await w.findAll('[data-test=attack-row] textarea')[0].setValue('Drags a victim under.');
    await w.get('[data-test=save-creature]').trigger('submit');
    await flushPromises();
    const sent = calls.find((c) => c.method === 'POST' && c.path === '/gm/homebrew/creatures').body;
    expect(sent).toMatchObject({ name: 'Bog Hag', kind: 'npc', category: 'Hags', attack_dice: 'D6' });
    expect(sent.statblocks).toEqual([{ variant: '', fields: { Ferocity: '3', Size: '', Movement: '', Armor: '', HP: '' } }]);
    expect(sent.attacks).toEqual([{ roll: '1-3', name: 'Grasp', text: 'Drags a victim under.' }]);
    expect(w.get('[data-test=status]').text()).toContain('Saved');
    expect(w.findAll('[data-test=creature-row]')).toHaveLength(2);
  });

  it('editing sends a PUT to the creature\'s id', async () => {
    const w = await mountIt();
    await w.get('[data-test=creature-row]').trigger('click');
    await w.get('[data-test=f-name]').setValue('Frost Wight Elder');
    await w.get('[data-test=save-creature]').trigger('submit');
    await flushPromises();
    const put = calls.find((c) => c.method === 'PUT');
    expect(put.path).toBe('/gm/homebrew/creatures/frost-wight');
    expect(put.body.name).toBe('Frost Wight Elder');
    expect(w.findAll('[data-test=creature-row]').map((r) => r.text())).toEqual(['Frost Wight ElderUndead']);
  });

  it('deleting asks first', async () => {
    const w = await mountIt();
    await w.get('[data-test=creature-row]').trigger('click');
    await w.get('[data-test=delete-creature]').trigger('click');
    expect(w.get('[data-test=delete-confirm]').text()).toContain('Frost Wight');
    expect(calls.some((c) => c.method === 'DELETE')).toBe(false);
    await w.get('[data-test=delete-yes]').trigger('click');
    await flushPromises();
    expect(calls.some((c) => c.method === 'DELETE' && c.path === '/gm/homebrew/creatures/frost-wight')).toBe(true);
    expect(w.find('[data-test=creature-form]').exists()).toBe(false);
    expect(w.findAll('[data-test=creature-row]')).toHaveLength(0);
  });

  it('starts from a creature of the books: a copy in the form to change and save as your own', async () => {
    const w = await mountIt();
    await w.get('[data-test=copy-creature]').trigger('click');
    await vi.advanceTimersByTimeAsync(300);
    await flushPromises();
    await w.get('[data-test=copy-result]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=f-name]').element.value).toBe('Wolf (copy)');
    expect(w.get('[data-test=f-kind]').element.value).toBe('animal');
    expect(w.findAll('[data-test=stat-field]')).toHaveLength(2);
    expect(w.findAll('[data-test=attack-row] input')[0].element.value).toBe('1-2');
    expect(w.findAll('[data-test=attack-row] textarea')[0].element.value).toBe('D8 damage.');          // the text of an attack no longer starts with its name
    expect(w.findAll('[data-test=ability-row]')).toHaveLength(1);
    await w.get('[data-test=save-creature]').trigger('submit');
    await flushPromises();
    expect(calls.some((c) => c.method === 'POST')).toBe(true);                                // it is new: nothing of the book is changed
    expect(calls.some((c) => c.method === 'PUT')).toBe(false);
  });

  it('shows what the server refuses', async () => {
    vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => (opts.method === 'POST'
      ? { ok: false, status: 400, json: async () => ({ error: 'A creature needs a name.' }) }
      : { ok: true, status: 200, json: async () => ({ creatures: [] }) })));
    const w = await mountIt();
    await w.get('[data-test=new-creature]').trigger('click');
    await w.get('[data-test=f-name]').setValue('X');
    await w.get('[data-test=save-creature]').trigger('submit');
    await flushPromises();
    expect(w.get('[data-test=status]').text()).toBe('A creature needs a name.');
    expect(w.get('[data-test=status]').classes()).toContain('error');
  });
});
