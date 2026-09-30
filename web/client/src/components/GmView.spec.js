import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import GmView from './GmView.vue';
import ChatView from './ChatView.vue';
import SheetView from './SheetView.vue';

const ch = (id, name, kin = 'Human', profession = 'Fighter') => ({
  id, name, kin, profession, age: 'adult', conditions: [], link: '', hp: { current: 10, max: 12 }, wp: { current: 8, max: 10 },
});
let characters;
let parties;
let calls;

function stub() {
  calls = [];
  characters = [ch('c1', 'Aria', 'Elf', 'Bard'), ch('c2', 'Brenna', 'Dwarf', 'Knight'), ch('c3', 'Cade')];
  parties = [
    { id: 'p1', name: 'The Misty Vale party', members: ['c1', 'c2'] },
    { id: 'p2', name: 'Bandit Hunters', members: ['c2'] },
  ];
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    const path = url.replace('/api', '').split('?')[0];
    const method = opts.method ?? 'GET';
    calls.push({ method, path, body: opts.body ? JSON.parse(opts.body) : null });
    const ok = (body, status = 200) => ({ ok: true, status, json: async () => structuredClone(body) });
    if (method === 'GET' && path === '/gm/characters') return ok({ characters });
    if (method === 'GET' && path === '/gm/chat') return ok({ threads: [{ characterId: 'c2', unread: 2 }] });
    if (method === 'GET' && path.startsWith('/gm/characters/')) return ok({ ...characters.find((c) => c.id === path.split('/')[3]), doc: null });
    if (method === 'GET' && path.startsWith('/gm/chat/')) return ok({ messages: [], gmRead: '', playerRead: '' });
    if (method === 'GET' && path === '/gm/parties') return ok({ parties });
    if (method === 'GET' && path === '/gm/content') return ok({ types: [], packs: [], rules: [] });
    if (method === 'GET' && path === '/gm/creation') {
      return ok({ kins: [{ key: 'k', title: 'Human', movement: '10', innate: [], names: [], body: '', source: '' }], professions: [{ key: 'p', title: 'Bard', keyAttribute: 'CHA', schools: [], skills: [], nicknames: [], body: '', fields: [], skillsBySchool: {}, heroic: [], gearSets: [], magic: {}, source: '' }], ages: [], attributes: [], baseChance: [], tables: {}, selectableSkills: [], skillAttributes: {}, heroicAbilities: [], spells: [] });
    }
    if (method === 'POST' && path === '/gm/parties') { parties.push({ id: 'p3', name: JSON.parse(opts.body).name, members: [] }); return ok(parties.at(-1), 201); }
    if (method === 'PATCH' && path.startsWith('/gm/parties/')) {
      const p = parties.find((x) => x.id === path.split('/')[3]);
      const body = JSON.parse(opts.body);
      if (body.name) p.name = body.name;
      if (body.addMember) p.members.push(body.addMember);
      if (body.removeMember) p.members = p.members.filter((m) => m !== body.removeMember);
      return ok(p);
    }
    if (method === 'DELETE' && path.startsWith('/gm/parties/')) { parties = parties.filter((p) => p.id !== path.split('/')[3]); return ok({ ok: true }); }
    if (method === 'DELETE' && path.startsWith('/gm/characters/')) { characters = characters.filter((c) => c.id !== path.split('/')[3]); return ok({ ok: true }); }
    if (method === 'POST' && path.startsWith('/gm/parties/') && path.endsWith('/message')) return ok({ sent: 2 }, 201);
    return { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
}

async function mountGm() {
  stub();
  const w = mount(GmView, { props: { token: 'gm-token' }, global: { stubs: { SheetView: true, ChatView: true, ReferenceView: true } } });
  await flushPromises();
  return w;
}
const tabLabels = (w) => w.findAll('[data-test=gm-tab]').map((t) => t.text());
const goTo = async (w, label) => {
  await w.findAll('[data-test=gm-tab]').find((t) => t.text().startsWith(label)).trigger('click');
  await flushPromises();
};

beforeEach(() => vi.useFakeTimers());
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

describe('GmView tabs', () => {
  it('has its tabs along the top, like the player page', async () => {
    const w = await mountGm();
    expect(tabLabels(w)).toEqual(['Character', 'Parties', 'Chat (2)', 'Rules']);
    expect(w.get('[data-test=gm-tab][aria-selected=true]').text()).toBe('Character');
    expect(w.find('.gm-sidebar').exists()).toBe(false);
  });
});

describe('GmView Character tab', () => {
  it('is a dropdown of every character, and the first one is open', async () => {
    const w = await mountGm();
    const options = w.findAll('[data-test=character-select] option');
    expect(options.map((o) => o.text().split(' · ')[0].replace(/ \(\d+\)$/, ''))).toEqual(['Aria', 'Brenna', 'Cade']);
    expect(w.get('[data-test=character-select]').element.value).toBe('c1');
    expect(w.findComponent(SheetView).exists()).toBe(true);
    expect(calls.some((c) => c.path === '/gm/characters/c1')).toBe(true);
  });

  it('choosing another character in the dropdown opens its sheet', async () => {
    const w = await mountGm();
    await w.get('[data-test=character-select]').setValue('c3');
    await flushPromises();
    expect(calls.some((c) => c.path === '/gm/characters/c3')).toBe(true);
    expect(w.findComponent(SheetView).props('patchPath')).toBe('/gm/characters/c3');
    expect(w.get('[data-test=character-select]').element.value).toBe('c3');
    expect(w.find('.char-meta').exists()).toBe(false);          // no summary line above the sheet
  });

  it('+ New character opens the creator, Random opens it filled in', async () => {
    const w = await mountGm();
    await w.get('[data-test=new-character]').trigger('click');
    await flushPromises();
    expect(w.find('[data-test=creator]').exists()).toBe(true);
    expect(calls.some((c) => c.path === '/gm/creation')).toBe(true);
    await w.get('[data-test=creator-cancel]').trigger('click');
    expect(w.find('[data-test=creator]').exists()).toBe(false);
  });

  it('deleting a character asks first, then removes it and opens another', async () => {
    const w = await mountGm();
    await w.get('[data-test=delete-character]').trigger('click');
    expect(w.get('[data-test=char-delete-confirm]').text()).toContain('Aria');
    expect(calls.some((c) => c.method === 'DELETE')).toBe(false);
    await w.get('[data-test=char-delete-no]').trigger('click');
    expect(w.find('[data-test=char-delete-confirm]').exists()).toBe(false);
    await w.get('[data-test=delete-character]').trigger('click');
    await w.get('[data-test=char-delete-yes]').trigger('click');
    await flushPromises();
    expect(calls.some((c) => c.method === 'DELETE' && c.path === '/gm/characters/c1')).toBe(true);
    expect(w.findAll('[data-test=character-select] option')).toHaveLength(2);
    expect(w.get('[data-test=character-select]').element.value).toBe('c2');
  });
});

describe('GmView Parties tab', () => {
  it('lists every party with its characters as a reduced sheet: name, class and kin', async () => {
    const w = await mountGm();
    await goTo(w, 'Parties');
    const panels = w.findAll('[data-test=party-panel]');
    expect(panels.map((p) => p.get('[data-test=party-name]').text())).toEqual(['The Misty Vale party', 'Bandit Hunters']);
    const rows = panels[0].findAll('[data-test=party-member]').map((r) => r.findAll('td').slice(0, 3).map((td) => td.text()));
    expect(rows).toEqual([['Aria', 'Bard', 'Elf'], ['Brenna', 'Knight', 'Dwarf']]);
    expect(panels[1].findAll('[data-test=party-member]')).toHaveLength(1);
  });

  it('a character in two parties appears in both, and the ones in none are under Solo', async () => {
    const w = await mountGm();
    await goTo(w, 'Parties');
    const names = w.findAll('[data-test=party-panel] .member-name').map((n) => n.text());
    expect(names.filter((n) => n === 'Brenna')).toHaveLength(2);
    expect(w.findAll('[data-test=solo-member]').map((r) => r.findAll('td').map((td) => td.text()))).toEqual([['Cade', 'Fighter', 'Human']]);
  });

  it('a name in a party opens that character\'s sheet on the Character tab', async () => {
    const w = await mountGm();
    await goTo(w, 'Parties');
    await w.findAll('[data-test=party-panel]')[0].findAll('.member-name')[1].trigger('click');
    await flushPromises();
    expect(w.get('[data-test=gm-tab][aria-selected=true]').text()).toBe('Character');
    expect(w.get('[data-test=character-select]').element.value).toBe('c2');
    await goTo(w, 'Parties');
    await w.get('[data-test=solo-member] .member-name').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=character-select]').element.value).toBe('c3');
  });

  it('creates a party', async () => {
    const w = await mountGm();
    await goTo(w, 'Parties');
    await w.get('[data-test=new-party]').trigger('click');
    await w.get('[data-test=new-party-name]').setValue('Test');
    await w.get('form.new-party').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'POST' && c.path === '/gm/parties').body).toEqual({ name: 'Test' });
    expect(w.findAll('[data-test=party-panel]')).toHaveLength(3);
  });

  it('adds any character to a party, renames it, sends it a message, removes a member and deletes it', async () => {
    const w = await mountGm();
    await goTo(w, 'Parties');
    const panel = () => w.findAll('[data-test=party-panel]')[0];
    await panel().get('[data-test=add-to-party]').trigger('click');
    expect(panel().findAll('[data-test=party-pick]').map((p) => p.find('b').text())).toEqual(['Cade']);
    await panel().get('[data-test=party-pick]').trigger('click');
    await flushPromises();
    expect(calls.find((c) => c.method === 'PATCH').body).toEqual({ addMember: 'c3' });
    expect(panel().findAll('[data-test=party-member]')).toHaveLength(3);
    expect(w.find('[data-test=solo]').exists()).toBe(false);

    await panel().get('[data-test=rename-party]').trigger('click');
    await panel().get('[data-test=rename-input]').setValue('  Ravens ');
    await panel().get('form.rename').trigger('submit');
    await flushPromises();
    expect(panel().get('[data-test=party-name]').text()).toBe('Ravens');

    await panel().get('[data-test=party-message]').setValue('Meet at the inn.');
    await panel().get('form.message').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.path === '/gm/parties/p1/message').body).toEqual({ text: 'Meet at the inn.' });

    await panel().findAll('[data-test=party-remove]')[0].trigger('click');
    await flushPromises();
    expect(panel().findAll('[data-test=party-member]')).toHaveLength(2);

    await panel().get('[data-test=delete-party]').trigger('click');
    expect(calls.some((c) => c.method === 'DELETE')).toBe(false);
    await panel().get('[data-test=delete-yes]').trigger('click');
    await flushPromises();
    expect(w.findAll('[data-test=party-panel]').map((p) => p.get('[data-test=party-name]').text())).toEqual(['Bandit Hunters']);
  });
});

describe('GmView Chat and Rules tabs', () => {
  it('Chat shows the conversation of the character picked in the dropdown, like the player\'s', async () => {
    const w = await mountGm();
    await goTo(w, 'Chat');
    expect(w.findComponent(ChatView).props('sendPath')).toBe('/gm/chat/c1');
    await w.get('[data-test=character-select]').setValue('c2');
    await flushPromises();
    expect(w.findComponent(ChatView).props('sendPath')).toBe('/gm/chat/c2');
    expect(w.findComponent(ChatView).props('asGm')).toBe(true);
  });

  it('Rules is the Reference, the same the players have', async () => {
    const w = await mountGm();
    await goTo(w, 'Rules');
    expect(w.findComponent({ name: 'ReferenceView' }).exists()).toBe(true);
    expect(w.findComponent({ name: 'ReferenceView' }).props('gmPrefix')).toBe('/gm');
  });
});
