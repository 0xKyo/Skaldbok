import { afterEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import NpcCreator from './NpcCreator.vue';

const lists = {
  lists: [
    { key: 'name', dice: 'D20', values: ['Agnar', 'Jorid'] },
    { key: 'attitude', dice: 'D4', values: ['Hostile', 'Friendly'] },
    { key: 'kin', dice: 'D6', values: ['Human'] },
  ],
};

const mountIt = async () => {
  vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => structuredClone(lists) })));
  const w = mount(NpcCreator, { props: { token: 'gm-token', gmPrefix: '/gm' } });
  await flushPromises();
  return w;
};

afterEach(() => vi.unstubAllGlobals());

describe('NpcCreator', () => {
  it('shows nothing until Create Random NPC is pressed, then one value per list', async () => {
    const w = await mountIt();
    expect(w.find('[data-test=npc]').exists()).toBe(false);
    await w.get('[data-test=npc-create]').trigger('click');
    const values = w.findAll('[data-test=npc-value]').map((v) => v.text());
    expect(values).toHaveLength(3);
    expect(lists.lists[0].values).toContain(values[0]);
    expect(values[2]).toBe('Human');
  });

  it('re-rolls a single attribute and leaves the others alone', async () => {
    const w = await mountIt();
    await w.get('[data-test=npc-create]').trigger('click');
    const before = w.findAll('[data-test=npc-value]').map((v) => v.text());
    await w.findAll('.reroll')[0].trigger('click');
    const after = w.findAll('[data-test=npc-value]').map((v) => v.text());
    expect(after[0]).not.toBe(before[0]);
    expect(after.slice(1)).toEqual(before.slice(1));
  });
});
