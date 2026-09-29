import { afterEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import RulesView from './RulesView.vue';

const routes = {
  '/content': { types: [], packs: [], rules: [{ key: 'actions', title: 'Actions' }] },
  '/rules/actions': {
    key: 'actions',
    title: 'Actions',
    intro: { body: 'On your turn you can move and act.', sections: [], tables: [] },
    rules: [
      { key: 'r/actions', title: 'Actions', body: 'Most common actions.', sections: [], tables: [{ title: 'Actions', dice: '', columns: ['ACTION', 'EFFECT'], rows: [{ roll: '', cells: ['Dash', 'Doubles your movement.'] }] }] },
      { key: 'r/free', title: 'Free Actions', body: 'Minor things.', sections: [], tables: [{ title: 'Free Actions', dice: '', columns: ['FREE ACTION', 'EFFECT'], rows: [{ roll: '', cells: ['Shout', 'Say a few words.'] }] }] },
      { key: 'r/move', title: 'Movement', body: 'Meters you can move.', sections: [], tables: [] },
    ],
  },
};

async function mountIt() {
  vi.stubGlobal('fetch', vi.fn(async (url) => {
    const body = routes[url.replace('/api', '').split('?')[0]];
    return body ? { ok: true, status: 200, json: async () => structuredClone(body) } : { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
  const w = mount(RulesView, { props: { token: 't', mode: 'rules', jumpTo: { type: 'actions', key: null }, hideTabs: true } });
  await flushPromises();
  return w;
}

const names = (w) => w.findAll('.rules-row-name').map((n) => n.text());

afterEach(() => vi.unstubAllGlobals());

describe('RulesView search in a rules chapter', () => {
  it('has a search bar and filters the chapter by title, text and table contents', async () => {
    const w = await mountIt();
    expect(names(w)).toEqual(['Actions', 'Free Actions', 'Movement']);
    const box = w.get('input[type=search]');
    await box.setValue('shout');
    expect(names(w)).toEqual(['Free Actions']);
    await box.setValue('meters');
    expect(names(w)).toEqual(['Movement']);
    await box.setValue('');
    expect(names(w)).toHaveLength(3);
  });

  it("shows the chapter's own text as the intro above the list, not as an entry", async () => {
    const w = await mountIt();
    expect(w.get('.inline-intro').text()).toContain('On your turn you can move and act.');
    expect(names(w)).not.toContain('On your turn you can move and act.');
  });

  it('keeps a valid entry selected and says so when nothing matches', async () => {
    const w = await mountIt();
    const box = w.get('input[type=search]');
    await box.setValue('shout');
    expect(w.get('.rules-detail-name').text()).toBe('Free Actions');
    await box.setValue('zzzz');
    expect(w.text()).toContain('Nothing found');
  });
});
