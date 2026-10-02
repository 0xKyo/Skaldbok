import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import { useReference } from '../reference.js';
import ReferenceView from './ReferenceView.vue';

const routes = {
  '/content': { types: [{ id: 'spells', label: 'Spells', count: 2 }, { id: 'creatures', label: 'Creatures', count: 1 }], rules: [{ key: 'combat', title: 'Combat' }] },
  '/content/spells': { entries: [{ key: 'core/spell/fireball', name: 'Fireball', subtitle: 'Rank 3' }] },
  '/creatures': { creatures: [{ id: 1, key: 'fire-imp', name: 'Fire Imp', sub: 'Demon' }] },
  '/rules/combat': { rules: [{ key: 'combat/initiative', title: 'Initiative', body: 'Whoever lights a fireball first.', sections: [], tables: [] }, { key: 'combat/armor', title: 'Armor', body: 'Heavy.', sections: [], tables: [] }] },
};

// the page itself is not under test: it only says what it was asked to show
const stubs = { ReferencePanel: { props: ['state'], template: '<div class="panel" data-test="panel">{{ state.current && state.current.label }}|{{ state.query }}|{{ state.only }}|{{ state.jump && state.jump.key }}</div>' } };
let urls;

beforeEach(() => {
  vi.useFakeTimers();
  urls = [];
  vi.stubGlobal('fetch', vi.fn(async (url) => {
    urls.push(url);
    let body = routes[url.replace('/api', '').split('?')[0]];
    const q = new URLSearchParams(url.split('?')[1] ?? '').get('q');
    if (body && q) body = { ...body, ...Object.fromEntries(['entries', 'creatures'].filter((k) => body[k]).map((k) => [k, body[k].filter((e) => e.name.toLowerCase().includes(q.toLowerCase()))])) };   // the server only answers with what matches
    return body ? { ok: true, status: 200, json: async () => structuredClone(body) } : { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
});
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

async function mountIt() {
  const state = useReference('t');
  await state.load();
  const w = mount(ReferenceView, { props: { state, token: 't' }, global: { stubs } });
  return { w, state };
}
const type = async (w, text) => {
  await w.get('[data-test=ref-search]').setValue(text);
  await vi.advanceTimersByTimeAsync(400);
  await flushPromises();
};

describe('the search of the Reference', () => {
  it('is one for everything: it lists matches from every category, each with the category it is in', async () => {
    const { w } = await mountIt();
    expect(w.find('[data-test=ref-only]').exists()).toBe(false);                   // nothing is open: there is no "only in"
    await type(w, 'fire');
    const hits = w.findAll('[data-test=ref-hit]').map((h) => h.text());
    expect(hits).toHaveLength(3);
    expect(hits.join('|')).toContain('Fireball');
    expect(hits.join('|')).toContain('Fire Imp');
    expect(hits.join('|')).toContain('Initiative');                                // found in the text of a rules chapter
    expect(hits.find((h) => h.includes('Initiative'))).toContain('Combat');
    expect(hits.find((h) => h.includes('Fireball'))).toContain('Spells');
    expect(w.find('[data-test=panel]').exists()).toBe(false);
  });

  it('says when nothing matches', async () => {
    const { w } = await mountIt();
    await type(w, 'zzzz');
    expect(w.get('[data-test=ref-none]').text()).toContain('zzzz');
  });

  it('a result opens its page on that entry, with the search kept on that page', async () => {
    const { w, state } = await mountIt();
    await type(w, 'fire');
    await w.findAll('[data-test=ref-hit]').find((h) => h.text().includes('Fireball')).trigger('click');
    await flushPromises();
    expect(state.current.type).toBe('spells');
    expect(state.jump.key).toBe('core/spell/fireball');
    expect(w.get('[data-test=panel]').text()).toContain('Spells|fire|true');
    await w.findAll('[data-test=ref-hit]');                                          // (the page replaced the list)
    expect(w.find('[data-test=ref-results]').exists()).toBe(false);
  });

  it('a result of a rules chapter opens that rule', async () => {
    const { w, state } = await mountIt();
    await type(w, 'fire');
    await w.findAll('[data-test=ref-hit]').find((h) => h.text().includes('Initiative')).trigger('click');
    expect(state.current.type).toBe('combat');
    expect(state.jump.key).toBe('combat/initiative');
  });

  it('with a category open, "Only in <category>" is checked and the page gets the search; unchecked, the matches are listed from everywhere', async () => {
    const { w, state } = await mountIt();
    state.select(state.items.find((i) => i.type === 'spells'));
    await flushPromises();
    expect(w.get('[data-test=ref-only-label]').text()).toBe('Only in Spells');
    expect(w.get('[data-test=ref-only]').element.checked).toBe(true);
    await type(w, 'fire');
    expect(w.find('[data-test=ref-results]').exists()).toBe(false);
    expect(w.get('[data-test=panel]').text()).toContain('Spells|fire|true');
    await w.get('[data-test=ref-only]').setValue(false);
    await vi.advanceTimersByTimeAsync(400);
    await flushPromises();
    expect(w.findAll('[data-test=ref-hit]').length).toBeGreaterThan(1);
    await w.get('[data-test=ref-only]').setValue(true);
    expect(w.find('[data-test=ref-results]').exists()).toBe(false);
  });

  it('an empty search shows the page as usual and asks for nothing', async () => {
    const { w, state } = await mountIt();
    state.select(state.items[0]);
    await flushPromises();
    const before = urls.length;
    await type(w, '   ');
    expect(w.find('[data-test=panel]').exists()).toBe(true);
    expect(urls.length).toBe(before);
  });
});
