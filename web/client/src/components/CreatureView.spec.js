import { afterEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import CreatureView from './CreatureView.vue';

const creatures = {
  creatures: [
    { id: 1, key: 'a', name: 'Guard', sub: 'NPC', kind: 'npc' },
    { id: 2, key: 'b', name: 'Wolf', sub: 'Animal', kind: 'animal' },
    { id: 3, key: 'c', name: 'Dragon', sub: 'Dragons', kind: 'monster' },
    { id: 4, key: 'd', name: 'Troll', sub: 'Trolls', kind: 'monster' },
  ],
};

async function mountIt() {
  vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => structuredClone(creatures) })));
  const w = mount(CreatureView, { props: { token: 'gm' } });
  await flushPromises();
  return w;
}

const names = (w) => w.findAll('.c-name').map((n) => n.text());

afterEach(() => vi.unstubAllGlobals());

describe('CreatureView tags', () => {
  it('lists everything until a tag is picked, then only that kind', async () => {
    const w = await mountIt();
    expect(names(w)).toEqual(['Guard', 'Wolf', 'Dragon', 'Troll']);
    const tags = w.findAll('[data-test=creature-tag]');
    expect(tags.map((t) => t.text())).toEqual(['NPC', 'Animal', 'Monster']);
    await tags[2].trigger('click');
    expect(names(w)).toEqual(['Dragon', 'Troll']);
  });

  it('combines tags and clears the filter when they are toggled off', async () => {
    const w = await mountIt();
    const tags = w.findAll('[data-test=creature-tag]');
    await tags[0].trigger('click');
    await tags[1].trigger('click');
    expect(names(w)).toEqual(['Guard', 'Wolf']);
    await tags[0].trigger('click');
    await tags[1].trigger('click');
    expect(names(w)).toHaveLength(4);
  });
});
