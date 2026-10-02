import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import HomebrewAdventure from './HomebrewAdventure.vue';

let adventures;       // id -> {id, title, chapters}
let calls;

const listOf = () => ({ adventures: Object.values(adventures).map((a) => ({ id: a.id, title: a.title, chapters: a.chapters.length })) });

beforeEach(() => {
  vi.useFakeTimers();
  adventures = {};
  calls = [];
  URL.createObjectURL = vi.fn(() => 'blob:x');
  URL.revokeObjectURL = vi.fn();
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    const path = url.replace('/api', '').split('?')[0];
    const method = opts.method ?? 'GET';
    const body = opts.body ? JSON.parse(opts.body) : null;
    calls.push({ method, path, body });
    const ok = (data, status = 200) => ({ ok: true, status, json: async () => structuredClone(data), blob: async () => new Blob(['x']) });
    if (path === '/gm/homebrew/adventures' && method === 'GET') return ok(listOf());
    if (path === '/gm/homebrew/adventures' && method === 'POST') {
      const id = body.title.toLowerCase().replace(/[^a-z0-9]+/g, '-');
      adventures[id] = { id, title: body.title, chapters: [] };
      return ok({ id, title: body.title }, 201);
    }
    const one = path.match(/^\/gm\/homebrew\/adventures\/([^/]+)(\/images)?$/);
    if (one) {
      const adventure = adventures[one[1]];
      if (one[2]) return ok({ path: 'images/uploads/u1.png' }, 201);
      if (method === 'GET') return ok(adventure);
      if (method === 'PUT') { adventures[one[1]] = { ...adventure, title: body.title, chapters: body.chapters }; return ok({ ok: true }); }
      if (method === 'DELETE') { delete adventures[one[1]]; return ok(listOf()); }
    }
    if (path.startsWith('/gm/creatures')) return ok({ creatures: [{ id: 1, key: 'core/goblin', name: 'Goblin', kind: 'monster', sub: 'Monster' }, { id: 2, key: 'core/mara', name: 'Mara', kind: 'npc', sub: 'Innkeeper' }] });
    if (path.startsWith('/gm/adventures/')) return ok({ creatures: { 'core/goblin': { name: 'Goblin' } } });
    return { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
});
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

const mountIt = async () => {
  const w = mount(HomebrewAdventure, { props: { token: 't' } });
  await flushPromises();
  return w;
};
const click = async (w, test) => { await w.get(`[data-test=${test}]`).trigger('click'); await flushPromises(); };
const names = (w) => w.findAll('[data-test=adv-row]').map((r) => r.text());
const settle = async () => { await vi.advanceTimersByTimeAsync(1000); await flushPromises(); };
const lastSave = () => calls.filter((c) => c.method === 'PUT').at(-1)?.body;

async function withAdventure() {
  const w = await mountIt();
  await w.get('[data-test=adv-name-new]').setValue('The Stolen Bell');
  await w.get('.adv-new').trigger('submit'); await flushPromises();
  return w;
}

describe('HomebrewAdventure', () => {
  it('starts by asking for a name, and an adventure is made with it', async () => {
    const w = await mountIt();
    expect(w.get('[data-test=adv-empty]').text()).toContain('Start with a name');
    expect(w.get('[data-test=adv-create]').attributes('disabled')).toBeDefined();
    await w.get('[data-test=adv-name-new]').setValue('The Stolen Bell');
    await w.get('.adv-new').trigger('submit'); await flushPromises();
    expect(calls.find((c) => c.method === 'POST').body).toEqual({ title: 'The Stolen Bell' });
    expect(w.get('[data-test=adv-title]').element.value).toBe('The Stolen Bell');
    expect(w.get('[data-test=adv-hint]').text()).toContain('Add a chapter');
  });

  it('chapters and sections inside them, with names the GM chooses, saved by themselves', async () => {
    const w = await withAdventure();
    await click(w, 'add-chapter');
    await w.get('[data-test=node-title]').setValue('The Village');
    await click(w, 'add-section');
    await w.get('[data-test=node-title]').setValue('The Inn');
    await w.get('[data-test=node-kind]').setValue('location');
    await w.get('[data-test=node-number]').setValue('1');
    await w.get('[data-test=node-body]').setValue('Smoky and warm.');
    await click(w, 'add-section');                                              // a subsection of the inn
    await w.get('[data-test=node-title]').setValue('The Cellar');
    expect(names(w)).toEqual(['The Village', '1.The Inn', 'The Cellar']);
    expect(lastSave()).toBeUndefined();                                         // nothing yet: it waits a moment after the last change
    await settle();
    const saved = lastSave();
    expect(saved.title).toBe('The Stolen Bell');
    expect(saved.chapters[0]).toMatchObject({ title: 'The Village', kind: 'chapter' });
    expect(saved.chapters[0].sections[0]).toMatchObject({ title: 'The Inn', kind: 'location', number: '1', body: 'Smoky and warm.' });
    expect(saved.chapters[0].sections[0].sections[0].title).toBe('The Cellar');
    expect(w.get('[data-test=adv-status]').text()).toBe('Saved');
  });

  it('a chapter has no kind to choose; parts can be moved up and down, and deleted after asking if they hold something', async () => {
    const w = await withAdventure();
    await click(w, 'add-chapter');
    expect(w.find('[data-test=node-kind]').exists()).toBe(false);
    await click(w, 'add-chapter');
    expect(names(w)).toEqual(['Chapter 1', 'Chapter 2']);
    await click(w, 'move-up');
    expect(names(w)).toEqual(['Chapter 2', 'Chapter 1']);
    expect(w.get('[data-test=move-up]').attributes('disabled')).toBeDefined();
    await click(w, 'add-section');
    await click(w, 'delete-node');                                                // the section is empty: gone at once
    expect(names(w)).toEqual(['Chapter 2', 'Chapter 1']);
    await w.findAll('[data-test=adv-row]')[0].trigger('click');
    await w.get('[data-test=node-body]').setValue('Something written');
    await click(w, 'delete-node');
    expect(w.get('[data-test=delete-node-confirm]').exists()).toBe(true);
    expect(names(w)).toHaveLength(2);
    await click(w, 'delete-node-yes');
    expect(names(w)).toEqual(['Chapter 1']);
  });

  it('a picture is uploaded to the adventure and kept in the part; it can be removed', async () => {
    const w = await withAdventure();
    await click(w, 'add-chapter');
    const input = w.get('[data-test=picture-file]');
    Object.defineProperty(input.element, 'files', { value: [new File(['x'], 'map.png', { type: 'image/png' })], configurable: true });
    await input.trigger('change');
    await vi.waitFor(() => expect(calls.some((c) => c.path.endsWith('/images'))).toBe(true));
    await flushPromises();
    expect(calls.find((c) => c.path.endsWith('/images')).body.image).toBeTruthy();
    await settle();
    expect(lastSave().chapters[0].images).toEqual(['images/uploads/u1.png']);
    expect(w.findAll('[data-test=node-picture]')).toHaveLength(1);
    await click(w, 'picture-remove');
    await settle();
    expect(lastSave().chapters[0].images).toEqual([]);
  });

  it('a creature of the Reference is added as a part of its own (an NPC or a monster) linked to it', async () => {
    const w = await withAdventure();
    await click(w, 'add-chapter');
    const pick = w.get('[data-test=creature-pick]');
    expect(pick.findAll('optgroup').map((g) => g.attributes('label'))).toEqual(['NPCs', 'Monsters']);          // grouped by kind
    expect(pick.findAll('option').map((o) => o.text())).toEqual(['Add an NPC or a monster from the Reference…', 'Mara · Innkeeper', 'Goblin · Monster']);
    await pick.setValue('core/mara');                                              // an NPC
    await flushPromises();
    expect(names(w)).toEqual(['Chapter 1', 'Mara']);
    expect(w.get('[data-test=node-kind]').element.value).toBe('npc');
    expect(w.get('[data-test=node-creature]').text()).toContain('Mara');
    await settle();
    expect(lastSave().chapters[0].sections[0]).toMatchObject({ title: 'Mara', kind: 'npc', creatures: ['core/mara'] });
    await w.get('[data-test=node-creature] .x').trigger('click');                 // unlink it
    expect(w.find('[data-test=node-creature]').exists()).toBe(false);
  });

  it('opens an adventure that is there, names the creatures it already has, and deletes it after asking', async () => {
    adventures.bell = { id: 'bell', title: 'Bell', chapters: [{ id: 'c1', title: 'One', kind: 'chapter', sections: [{ id: 'g', title: 'Goblin', kind: 'monster', creatures: ['core/goblin'] }] }] };
    const w = await mountIt();
    expect(names(w)).toEqual(['One', 'Goblin']);
    await w.findAll('[data-test=adv-row]')[1].trigger('click');
    expect(w.get('[data-test=node-creature]').text()).toContain('Goblin');
    expect(lastSave()).toBeUndefined();                                           // loading is not a change
    await settle();
    expect(lastSave()).toBeUndefined();
    await click(w, 'adv-delete');
    expect(w.get('[data-test=adv-delete-confirm]').text()).toContain('Bell');
    await click(w, 'adv-delete-yes');
    expect(calls.some((c) => c.method === 'DELETE')).toBe(true);
    expect(w.find('[data-test=adv-empty]').exists()).toBe(true);
  });

  it('shows what the server says when it cannot save', async () => {
    const w = await withAdventure();
    await click(w, 'add-chapter');
    fetch.mockImplementationOnce(async () => ({ ok: false, status: 400, json: async () => ({ error: 'The adventure is too big or deep.' }) }));
    await w.get('[data-test=node-title]').setValue('X');
    await settle();
    expect(w.get('[data-test=adv-status]').text()).toContain('too big');
  });
});
