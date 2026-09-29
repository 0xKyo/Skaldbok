import { afterEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import RulesView from './RulesView.vue';

const table = (title, rows) => ({ title, dice: '', columns: ['ACTION', 'EFFECT'], rows: rows.map(([name, effect]) => ({ roll: '', cells: [name, effect] })) });

const routes = {
  '/content': { types: [], packs: [], rules: [{ key: 'actions', title: 'Actions' }] },
  '/rules/actions': {
    key: 'actions',
    title: 'Actions',
    layout: 'tabs',
    intro: { body: 'On your turn you can move and act.', sections: [], tables: [] },
    rules: [
      { key: 'r/actions', title: 'Actions', body: 'The common actions.', sections: [], tables: [table('Actions', [['Dash', 'Doubles your movement.'], ['Parry', 'A reaction.\nOutside your turn.']])] },
      { key: 'r/free', title: 'Free Actions', body: 'Minor things.', sections: [], tables: [table('Free Actions', [['Shout', 'Say a few words.']])] },
      { key: 'r/melee', title: 'Melee Combat', body: '', sections: [], tables: [table('Melee Combat Actions', [['Damage', 'Roll the weapon dice.']])] },
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

const tabs = (w) => w.findAll('[data-test=chapter-tab]').map((t) => t.text());
const rows = (w) => w.findAll('[data-test=table-row]').map((r) => r.text());

afterEach(() => vi.unstubAllGlobals());

describe('RulesView with layout: tabs', () => {
  it('turns the entries into tabs and the rows of the tab table into the list', async () => {
    const w = await mountIt();
    expect(tabs(w)).toEqual(['Actions', 'Free Actions', 'Melee Combat']);
    expect(rows(w)).toEqual(['Dash', 'Parry']);
    expect(w.text()).toContain('On your turn you can move and act.');
    expect(w.text()).toContain('The common actions.');
    await w.findAll('[data-test=chapter-tab]')[1].trigger('click');
    expect(rows(w)).toEqual(['Shout']);
    expect(w.text()).toContain('Minor things.');
  });

  it('shows the info of the chosen row beside the list', async () => {
    const w = await mountIt();
    expect(w.get('.rules-detail-name').text()).toBe('Dash');
    expect(w.get('.rules-detail').text()).toContain('Doubles your movement.');
    await w.findAll('[data-test=table-row]')[1].trigger('click');
    expect(w.get('.rules-detail-name').text()).toBe('Parry');
    expect(w.get('.rules-detail').text()).toContain('Outside your turn.');
  });

  it('searches the rows, moving to the tab that has a match, and an empty intro adds no panel', async () => {
    const w = await mountIt();
    await w.get('input[type=search]').setValue('shout');
    expect(rows(w)).toEqual(['Shout']);
    expect(w.findAll('[data-test=chapter-tab]')[1].attributes('aria-pressed')).toBe('true');
    await w.get('input[type=search]').setValue('');
    await w.findAll('[data-test=chapter-tab]')[2].trigger('click');
    expect(w.findAll('.inline-intro')).toHaveLength(1);
  });
});
