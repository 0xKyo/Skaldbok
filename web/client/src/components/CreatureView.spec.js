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
  intro: { body: '', sections: [{ title: 'Monsters', body: 'Monsters act several times a round.' }], tables: [] },
};

async function mountIt() {
  vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => structuredClone(creatures) })));
  const w = mount(CreatureView, { props: { token: 'gm', prefix: '/gm' } });
  await flushPromises();
  return w;
}

const names = (w) => w.findAll('.c-name').map((n) => n.text());

afterEach(() => vi.unstubAllGlobals());

describe('CreatureView page', () => {
  it('has the tabs first, then the search bar, and the Bestiary text lives in the General Info tab', async () => {
    const w = await mountIt();
    const page = w.get('.creature-page');
    expect(page.element.firstElementChild.classList.contains('subtabs')).toBe(true);
    expect(w.findAll('[data-test=creature-tab]').map((t) => t.text())).toEqual(['Creatures', 'General Info']);
    expect(w.find('.c-intro').exists()).toBe(false);
    expect(names(w)).toHaveLength(4);
    await w.findAll('[data-test=creature-tab]')[1].trigger('click');
    expect(w.get('.c-intro').text()).toContain('Monsters act several times a round.');
    expect(w.get('.c-layout').isVisible()).toBe(false);
    await w.findAll('[data-test=creature-tab]')[0].trigger('click');
    expect(w.find('.c-intro').exists()).toBe(false);
  });
});

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

  it('on a phone the list and the creature take turns: a tap opens the creature and Back returns to the list', async () => {
    const detail = { id: 1, key: 'a', name: 'Guard', kind: 'npc', category: '', pack: 'Core', image: null, blocks: [], attacks: [], abilities: [], description: '', body: '', tables: [], statsRef: '' };
    const w = await mountIt();
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => structuredClone(detail) })));
    expect(w.find('[data-test=creature-back]').exists()).toBe(false);
    expect(w.get('.c-layout').classes()).not.toContain('detail-open');
    await w.get('.c-row').trigger('click');
    await flushPromises();
    expect(w.get('.c-layout').classes()).toContain('detail-open');
    expect(w.get('[data-test=creature-back]').exists()).toBe(true);
    await w.get('[data-test=creature-back]').trigger('click');
    expect(w.get('.c-layout').classes()).not.toContain('detail-open');
  });
});
