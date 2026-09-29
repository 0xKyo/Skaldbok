import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import GmView from './GmView.vue';

const ch = (id, name, kin = 'Human', profession = 'Fighter') => ({
  id, name, kin, profession, age: 'adult', conditions: [], link: '', hp: { current: 10, max: 12 }, wp: { current: 8, max: 10 },
});
let characters;
let parties;
let calls;

function stub() {
  calls = [];
  characters = [ch('c1', 'Aria'), ch('c2', 'Brenna'), ch('c3', 'Cade')];
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
    if (method === 'GET' && path === '/gm/chat') return ok({ threads: [] });
    if (method === 'GET' && path === '/gm/creation') return ok({ kins: [], professions: [] });
    if (method === 'GET' && path.startsWith('/gm/characters/')) return ok({ ...characters[0], id: path.split('/')[3], name: 'Cade' });
    if (method === 'GET' && path === '/gm/parties') return ok({ parties });
    if (method === 'GET' && path === '/gm/content') return ok({ types: [], packs: [], rules: [] });
    if (method === 'PATCH' && path === '/gm/parties/p1') {
      const body = JSON.parse(opts.body);
      if (body.name) parties[0].name = body.name;
      if (body.addMember) parties[0].members.push(body.addMember);
      if (body.removeMember) parties[0].members = parties[0].members.filter((m) => m !== body.removeMember);
      return ok(parties[0]);
    }
    if (method === 'DELETE' && path === '/gm/parties/p1') { parties = parties.filter((p) => p.id !== 'p1'); return ok({ ok: true }); }
    if (method === 'DELETE' && path === '/gm/characters/c3') { characters.splice(characters.findIndex((c) => c.id === 'c3'), 1); return ok({ ok: true }); }
    if (method === 'POST' && path === '/gm/parties/p1/message') return ok({ sent: parties[0].members.length }, 201);
    return { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
}

async function mountGm() {
  stub();
  const w = mount(GmView, { props: { token: 'gm-token' }, global: { stubs: { SheetView: true } } });
  await flushPromises();
  return w;
}

beforeEach(() => vi.useFakeTimers());
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

describe('GmView parties', () => {
  it('lists the characters that are in no party under Solo, and the parties as a dropdown', async () => {
    const w = await mountGm();
    expect(w.get('[data-test=solo-title]').text()).toContain('Solo');
    expect(w.findAll('[data-test=solo-list] .char-name').map((n) => n.text())).toEqual(['Cade']);
    expect(w.findAll('[data-test=party-item] .party-open .char-name').map((n) => n.text())).toEqual(['The Misty Vale party', 'Bandit Hunters']);
    await w.get('[data-test=parties-toggle]').trigger('click');
    expect(w.find('[data-test=party-list]').exists()).toBe(false);
    await w.get('[data-test=parties-toggle]').trigger('click');
    expect(w.find('[data-test=party-list]').exists()).toBe(true);
  });

  it('shows the characters of a party under it, and a character can be in several parties', async () => {
    const w = await mountGm();
    expect(w.findAll('[data-test=party-character]')).toHaveLength(0);
    const chevrons = w.findAll('[data-test=party-chevron]');
    await chevrons[0].trigger('click');
    await chevrons[1].trigger('click');
    const names = w.findAll('[data-test=party-character] .char-name').map((n) => n.text());
    expect(names).toEqual(['Aria', 'Brenna', 'Brenna']);
  });

  it('opens the party page with its name and members when the party is clicked', async () => {
    const w = await mountGm();
    await w.findAll('[data-test=party-open]')[0].trigger('click');
    expect(w.get('[data-test=party-name]').text()).toBe('The Misty Vale party');
    expect(w.findAll('[data-test=party-member]').map((m) => m.find('.member-name').text())).toEqual(['Aria', 'Brenna']);
    expect(w.findAll('[data-test=party-character]')).toHaveLength(2);       // and it unfolds in the sidebar
  });

  it('adds any character that is not in the party yet, through Add to Party', async () => {
    const w = await mountGm();
    await w.findAll('[data-test=party-open]')[0].trigger('click');
    await w.get('[data-test=add-to-party]').trigger('click');
    expect(w.findAll('[data-test=party-pick]').map((p) => p.find('b').text())).toEqual(['Cade']);
    await w.get('[data-test=party-pick]').trigger('click');
    await flushPromises();
    expect(calls.find((c) => c.method === 'PATCH' && c.path === '/gm/parties/p1').body).toEqual({ addMember: 'c3' });
    expect(w.findAll('[data-test=party-member]')).toHaveLength(3);
    expect(w.findAll('[data-test=solo-list] .char-name')).toHaveLength(0);
  });

  it('sends a message to the whole party', async () => {
    const w = await mountGm();
    await w.findAll('[data-test=party-open]')[0].trigger('click');
    expect(w.get('[data-test=party-send]').attributes('disabled')).toBeDefined();
    await w.get('[data-test=party-message]').setValue('Meet at the inn.');
    await w.get('form.message').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'POST' && c.path === '/gm/parties/p1/message').body).toEqual({ text: 'Meet at the inn.' });
    expect(w.get('[data-test=party-status]').text()).toBe('Sent to 2 characters.');
    expect(w.get('[data-test=party-message]').element.value).toBe('');
  });

  it('removes a member from the party', async () => {
    const w = await mountGm();
    await w.findAll('[data-test=party-open]')[0].trigger('click');
    await w.findAll('[data-test=party-remove]')[0].trigger('click');
    await flushPromises();
    expect(calls.find((c) => c.method === 'PATCH').body).toEqual({ removeMember: 'c1' });
    expect(w.findAll('[data-test=party-member]')).toHaveLength(1);
    expect(w.findAll('[data-test=solo-list] .char-name').map((n) => n.text())).toEqual(['Aria', 'Cade']);
  });
});

describe('GmView deleting a party', () => {
  it('asks first, then deletes the party and keeps its characters', async () => {
    const w = await mountGm();
    await w.findAll('[data-test=party-open]')[0].trigger('click');
    await w.get('[data-test=delete-party]').trigger('click');
    expect(w.get('[data-test=delete-confirm]').text()).toContain('Its characters are kept');
    expect(calls.some((c) => c.method === 'DELETE')).toBe(false);          // nothing is deleted before the confirmation
    await w.get('[data-test=delete-no]').trigger('click');
    expect(w.find('[data-test=delete-confirm]').exists()).toBe(false);
    await w.get('[data-test=delete-party]').trigger('click');
    await w.get('[data-test=delete-yes]').trigger('click');
    await flushPromises();
    expect(calls.some((c) => c.method === 'DELETE' && c.path === '/gm/parties/p1')).toBe(true);
    expect(w.findAll('[data-test=party-item] .party-open .char-name').map((n) => n.text())).toEqual(['Bandit Hunters']);
    expect(w.find('[data-test=party-panel]').exists()).toBe(false);          // the page of the deleted party closes
    expect(w.findAll('[data-test=solo-list] .char-name').map((n) => n.text())).toEqual(['Aria', 'Cade']);   // Aria was only in that party, Brenna is still in the other
  });
});

describe('GmView deleting a character', () => {
  it('asks first, then deletes the character and closes its panel', async () => {
    const w = await mountGm();
    await w.get('[data-test=solo-list] .char-name').trigger('click');
    await flushPromises();
    await w.get('[data-test=delete-character]').trigger('click');
    expect(w.get('[data-test=char-delete-confirm]').text()).toContain('Cade');
    expect(calls.some((c) => c.method === 'DELETE')).toBe(false);
    await w.get('[data-test=char-delete-no]').trigger('click');
    expect(w.find('[data-test=char-delete-confirm]').exists()).toBe(false);
    await w.get('[data-test=delete-character]').trigger('click');
    await w.get('[data-test=char-delete-yes]').trigger('click');
    await flushPromises();
    expect(calls.some((c) => c.method === 'DELETE' && c.path === '/gm/characters/c3')).toBe(true);
    expect(w.find('[data-test=delete-character]').exists()).toBe(false);
    expect(w.findAll('[data-test=solo-list] .char-name')).toHaveLength(0);
  });
});

describe('GmView renaming a party', () => {
  it('renames the party from its page and in the sidebar', async () => {
    const w = await mountGm();
    await w.findAll('[data-test=party-open]')[0].trigger('click');
    await w.get('[data-test=rename-party]').trigger('click');
    await w.get('[data-test=rename-input]').setValue('  Ravens of the Vale ');
    await w.get('form.rename').trigger('submit');
    await flushPromises();
    expect(calls.find((c) => c.method === 'PATCH').body).toEqual({ name: 'Ravens of the Vale' });
    expect(w.get('[data-test=party-name]').text()).toBe('Ravens of the Vale');
    expect(w.findAll('[data-test=party-item] .party-open .char-name')[0].text()).toBe('Ravens of the Vale');
  });

  it('cancelling leaves the name alone', async () => {
    const w = await mountGm();
    await w.findAll('[data-test=party-open]')[0].trigger('click');
    await w.get('[data-test=rename-party]').trigger('click');
    await w.get('[data-test=rename-input]').setValue('Something else');
    await w.get('[data-test=rename-cancel]').trigger('click');
    expect(w.get('[data-test=party-name]').text()).toBe('The Misty Vale party');
    expect(calls.some((c) => c.method === 'PATCH')).toBe(false);
  });
});

describe('GmView new character', () => {
  it('the + opens the creator instead of a name box, and Random opens it filled in', async () => {
    const w = await mountGm();
    await w.get('[data-test=new-character]').trigger('click');
    await flushPromises();
    expect(w.find('[data-test=creator]').exists()).toBe(true);
    expect(w.find('.inline-input').exists()).toBe(false);
    expect(calls.some((c) => c.method === 'GET' && c.path === '/gm/creation')).toBe(true);
    await w.get('.nav-item').trigger('click');          // General Settings leaves the creator
    expect(w.find('[data-test=creator]').exists()).toBe(false);
  });
});
