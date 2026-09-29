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
      { key: 'r/actions', title: 'Actions', body: 'The common actions.', sections: [], tables: [table('Actions', [['Parry', 'A reaction.\nOutside your turn.'], ['Dash', 'Doubles your movement.']])] },
      { key: 'r/free', title: 'Free Actions', body: 'Minor things.', sections: [], tables: [{ title: 'Free Actions', dice: '', columns: ['FREE ACTION', 'EFFECT', 'Extra', 'Empty'], rows: [{ roll: '', cells: ['Shout', 'Say a few words.', 'More detail here.', ''] }] }] },
      { key: 'r/melee', title: 'Melee Combat', body: '', sections: [], tables: [table('Melee Combat Actions', [['Damage', 'Roll the weapon dice.\n* First bullet.\n* Second bullet.']])] },
      { key: 'r/info', title: 'General Info', body: 'How a round works.', sections: [{ title: 'Reactions', body: 'Some actions happen on the opponent turn.' }, { title: 'Terrain', body: 'Maps show the terrain.\n{{table: Terrain}}' }], tables: [table('Terrain', [['Cramped', 'Little space.'], ['Rough', 'Hard to cross.']])] },
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
    expect(tabs(w)).toEqual(['Actions', 'Free Actions', 'Melee Combat', 'General Info']);
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

  it('shows a tab without a table as plain text (General Info): its body and its sections, no list of rows', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=chapter-tab]')[3].trigger('click');
    const text = w.get('[data-test=tab-text]').text();
    expect(text).toContain('How a round works.');
    expect(text).toContain('Reactions');
    expect(text).toContain('Some actions happen on the opponent turn.');
    expect(text).toContain('Maps show the terrain.');
    expect(w.findAll('[data-test=tab-text] .data-table tbody tr')).toHaveLength(2);   // the Terrain table is drawn inside its section
    expect(w.findAll('[data-test=table-row]')).toHaveLength(0);
  });

  it('gives a row with extra fields a heading per filled field and nothing for the empty ones', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=chapter-tab]')[1].trigger('click');
    const detail = w.get('.rules-detail');
    expect(detail.text()).toContain('Say a few words.');
    expect(detail.text()).toContain('More detail here.');
    expect(detail.findAll('.rules-section-title').map((h) => h.text())).toEqual(['Extra']);
  });

  it('draws the bullets of a row as a real list', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=chapter-tab]')[2].trigger('click');
    const items = w.findAll('.rules-detail .rule-list li').map((li) => li.text());
    expect(items).toEqual(['First bullet.', 'Second bullet.']);
    expect(w.get('.rules-detail').text()).toContain('Roll the weapon dice.');
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

describe('RulesView with layout: groups', () => {
  const groupRoutes = {
    '/content': { types: [], packs: [], rules: [{ key: 'world', title: 'World' }] },
    '/rules/world': {
      key: 'world',
      title: 'World',
      layout: 'groups',
      intro: null,
      rules: [
        { key: 'r/journeys', title: 'Journeys', tab: 'Journeys', body: 'Travel by shifts.', sections: [], tables: [] },
        { key: 'r/npcs', title: 'Non-Player Characters', tab: 'NPC', body: 'You control the world.', sections: [], tables: [] },
        { key: 'r/creator', title: 'NPC Creator', tab: 'NPC', body: 'Roll a random NPC.', sections: [], tables: [] },
      ],
    },
  };

  async function mountWorld() {
    vi.stubGlobal('fetch', vi.fn(async (url) => {
      const body = groupRoutes[url.replace('/api', '').split('?')[0]];
      return body ? { ok: true, status: 200, json: async () => structuredClone(body) } : { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
    }));
    const w = mount(RulesView, { props: { token: 't', mode: 'rules', jumpTo: { type: 'world', key: null }, hideTabs: true } });
    await flushPromises();
    return w;
  }
  const groupTabs = (w) => w.findAll('[data-test=group-tab]').map((t) => t.text());
  const listed = (w) => w.findAll('.rules-row-name').map((n) => n.text());

  it('makes a tab of each group of rules and lists only the rules of the chosen tab', async () => {
    const w = await mountWorld();
    expect(groupTabs(w)).toEqual(['Journeys', 'NPC']);
    expect(listed(w)).toEqual(['Journeys']);
    expect(w.get('.rules-detail-name').text()).toBe('Journeys');
    await w.findAll('[data-test=group-tab]')[1].trigger('click');
    expect(listed(w)).toEqual(['Non-Player Characters', 'NPC Creator']);
    expect(w.get('.rules-detail-name').text()).toBe('Non-Player Characters');
  });

  it('searches across tabs, moving to the tab that has the match', async () => {
    const w = await mountWorld();
    await w.get('input[type=search]').setValue('random npc');
    expect(w.findAll('[data-test=group-tab]')[1].attributes('aria-pressed')).toBe('true');
    expect(listed(w)).toEqual(['NPC Creator']);
    expect(w.get('.rules-detail-name').text()).toBe('NPC Creator');
  });
});
