import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import AdventuresView from './AdventuresView.vue';

let calls;
let saved;                     // the notes the server holds

const misty = {
  id: 'mistyvale', name: 'Dragonbane: The Misty Vale', title: 'The Misty Vale',
  creatures: { 'core/monster/adventure-hardy': { name: 'Hardy', kind: 'npc' } },
  tables: { 'Outskirt: Random Events': { title: 'Outskirt: Random Events', dice: 'D6', columns: ['EVENT'], rows: [{ roll: '1', cells: ['A storm'] }, { roll: '2', cells: ['Nothing'] }] } },
  chapters: [
    { id: 'intro', title: 'Introduction', number: 1, kind: 'chapter', page: 5, body: 'Welcome to the book. Ask Hardy about Outskirt, and visit the South Gate.', sections: [
      { id: 'intro/history', title: 'History', kind: 'section', page: 8, body: 'A long time ago.' },
    ] },
    { id: 'outskirt', title: 'Outskirt', number: 3, kind: 'chapter', page: 16, keywords: ['Outskirt'], body: 'A village.', sections: [
      { id: 'outskirt/locations', title: 'Locations', kind: 'section', page: 21, sections: [
        { id: 'outskirt/locations/2a-south-gate', title: 'South Gate', number: '2a', kind: 'location', page: 21, keywords: ['South Gate'], images: ['images/maps/south-gate.jpg'], body: 'A wooden gate.\n✦Guards: Two villagers watch it.\n✦Gate: It is barred at night.', sections: [
          { id: 'outskirt/locations/2a-south-gate/hardy', title: 'Hardy', kind: 'npc', page: 21, keywords: ['Hardy'], images: ['images/people/hardy.png'], body: 'A war veteran. He guards the South Gate.', creatures: ['core/monster/adventure-hardy'] },
          { id: 'outskirt/locations/2a-south-gate/stubborn', title: 'Stubborn Players', kind: 'sidebar', page: 21, body: 'Let them wander.' },
        ] },
      ] },
      { id: 'outskirt/random-events', title: 'Random Events', kind: 'table', page: 18, table: 'Outskirt: Random Events' },
    ] },
  ],
};

beforeEach(() => {
  calls = [];
  saved = {};
  URL.createObjectURL = vi.fn(() => 'blob:picture');
  URL.revokeObjectURL = vi.fn();
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    const path = url.replace('/api', '').split('?')[0];
    calls.push(path);
    if (opts.method === 'PUT' && path === '/gm/adventures/mistyvale/notes') {
      const b = JSON.parse(opts.body);
      if (b.text.trim()) saved[b.node] = b.text; else delete saved[b.node];
      return { ok: true, status: 200, json: async () => ({ ok: true }) };
    }
    const ok = (data) => ({ ok: true, status: 200, json: async () => structuredClone(data) });
    if (path === '/gm/adventures') return ok({ adventures: [{ id: 'mistyvale', name: misty.name, title: misty.title, chapters: 2 }, { id: 'other', name: 'Another Tale', title: 'Another', chapters: 1 }] });
    if (path === '/gm/adventures/mistyvale/image') return { ok: true, status: 200, blob: async () => new Blob(['x']) };
    if (path === '/gm/adventures/mistyvale') return ok({ ...misty, notes: { ...saved } });
    if (path === '/gm/creatures/core%2Fmonster%2Fadventure-hardy') return ok({ name: 'Hardy', blocks: [{ variant: '', fields: [{ label: 'HP', value: '16' }, { label: 'Movement', value: '12' }] }], attacks: [{ rollText: '1-2', name: 'Broadsword', text: '2D6' }], abilities: [{ name: 'Veteran', text: 'Counts as experienced.' }] });
    if (path === '/gm/adventures/other') return ok({ id: 'other', name: 'Another Tale', title: 'Another', tables: {}, creatures: {}, chapters: [{ id: 'one', title: 'The Only Chapter', kind: 'chapter', page: 1, body: 'Short.' }] });
    return { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  }));
});
afterEach(() => vi.unstubAllGlobals());

const mountIt = async () => {
  const w = mount(AdventuresView, { props: { token: 'gm' } });
  await flushPromises();
  return w;
};

describe('AdventuresView', () => {
  it('offers the loaded adventures in a dropdown and opens the first', async () => {
    const w = await mountIt();
    expect(w.findAll('[data-test=adventure-pick] option').map((o) => o.text())).toEqual(['Dragonbane: The Misty Vale', 'Another Tale']);
    expect(w.findAll('[data-test=outline-item]').map((i) => i.text())).toEqual(['1.Introduction', '3.Outskirt']);
    expect(w.get('[data-test=page]').text()).toContain('Welcome to the book.');
  });

  it('changing the adventure opens the other one', async () => {
    const w = await mountIt();
    await w.get('[data-test=adventure-pick]').setValue('other');
    await flushPromises();
    expect(w.findAll('[data-test=outline-item]').map((i) => i.text())).toEqual(['The Only Chapter']);
    expect(w.get('[data-test=page]').text()).toContain('Short.');
  });

  it('the outline opens chapters, sections and locations; a chapter lists what is in it', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=outline-item]')[1].trigger('click');              // Outskirt
    expect(w.get('[data-test=contents]').text()).toContain('Locations');
    const labels = w.findAll('[data-test=outline-item]').map((i) => i.text());
    expect(labels).toContain('Locations');                                       // its section opened under it
    await w.findAll('[data-test=outline-item]').find((i) => i.text() === 'Locations').trigger('click');
    expect(w.findAll('[data-test=outline-item]').map((i) => i.text())).toContain('2a.South Gate');
  });

  it('a location shows its text, its bullets, and what is inside it: NPCs with their creature, sidebars', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=outline-item]')[1].trigger('click');
    await w.findAll('[data-test=outline-item]').find((i) => i.text() === 'Locations').trigger('click');
    await w.findAll('[data-test=outline-item]').find((i) => i.text() === '2a.South Gate').trigger('click');
    const page = w.get('[data-test=page]');
    expect(page.text()).toContain('A wooden gate.');
    expect(page.findAll('.bullets li').map((li) => li.text())).toEqual(['Guards. Two villagers watch it.', 'Gate. It is barred at night.']);
    const inner = page.findAll('[data-test=inner]').map((i) => i.text());
    expect(inner[0]).toContain('Hardy');
    expect(inner[0]).toContain('A war veteran.');
    expect(page.find('.creature').text()).toBe('Hardy');
    expect(inner[1]).toContain('Stubborn Players');
    expect(w.get('[data-test=trail]').text()).toContain('Outskirt');
  });

  it('a table inside a chapter is shown as a table, with its roll button', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=outline-item]')[1].trigger('click');
    const inner = w.findAll('[data-test=inner]').map((i) => i.text()).join(' ');
    expect(inner).toContain('Random Events');
    expect(inner).toContain('A storm');
    expect(w.find('[data-test=table-roll]').exists()).toBe(true);
  });

  it('the NPCs tab gathers every NPC of the adventure, by chapter, with its stat block from Core', async () => {
    const w = await mountIt();
    expect(w.get('[data-test=tab-npcs]').text()).toBe('NPCs (1)');
    await w.get('[data-test=tab-npcs]').trigger('click');
    expect(w.findAll('.ngroup').map((g) => g.text())).toEqual(['Outskirt']);
    expect(w.findAll('[data-test=npc-row]').map((r) => r.text())).toEqual(['HardyLocations › 2a. South Gate']);
    await w.get('[data-test=npc-row]').trigger('click');
    await flushPromises();
    const page = w.get('[data-test=npc-page]');
    expect(page.text()).toContain('A war veteran.');
    expect(page.get('[data-test=npc-place]').text()).toContain('Outskirt › Locations › 2a. South Gate');
    expect(page.get('[data-test=npc-stats]').text()).toContain('Movement');
    expect(page.get('[data-test=npc-stats]').text()).toContain('Broadsword');
    expect(page.get('[data-test=npc-stats]').text()).toContain('Veteran');
  });

  it('the NPCs can be searched, monsters can be added, and one can be opened in the story', async () => {
    const w = await mountIt();
    await w.get('[data-test=tab-npcs]').trigger('click');
    await w.get('[data-test=npc-search]').setValue('zzz');
    expect(w.findAll('[data-test=npc-row]')).toHaveLength(0);
    await w.get('[data-test=npc-search]').setValue('');
    await w.get('[data-test=npc-row]').trigger('click');
    await flushPromises();
    await w.get('[data-test=npc-story]').trigger('click');
    expect(w.find('[data-test=npcs]').exists()).toBe(false);
    expect(w.get('[data-test=page]').text()).toContain('A wooden gate.');                    // the location it is in
  });

  it('an NPC met in the story opens its own page in the NPCs tab, and "Open in the story" brings you back', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=outline-item]')[1].trigger('click');
    await w.findAll('[data-test=outline-item]').find((i) => i.text() === 'Locations').trigger('click');
    await w.findAll('[data-test=outline-item]').find((i) => i.text() === '2a.South Gate').trigger('click');
    expect(w.findAll('[data-test=story-npc]')).toHaveLength(1);                         // Hardy is a link; the sidebar is not
    await w.get('[data-test=story-npc]').trigger('click');
    await flushPromises();
    expect(w.find('[data-test=npcs]').exists()).toBe(true);
    expect(w.get('[data-test=npc-page]').text()).toContain('Hardy');
    expect(w.get('[data-test=npc-page]').text()).toContain('Broadsword');
    await w.get('[data-test=npc-story]').trigger('click');
    expect(w.get('[data-test=page]').text()).toContain('A wooden gate.');
  });

  it('the words the adventure says matter are links: to a chapter, with a way back', async () => {
    const w = await mountIt();
    const links = w.findAll('[data-test=kw]').map((a) => a.text());
    expect(links).toEqual(['Hardy', 'Outskirt', 'South Gate']);
    await w.findAll('[data-test=kw]')[1].trigger('click');                                  // Outskirt
    expect(w.get('[data-test=page]').text()).toContain('A village.');
    expect(w.findAll('[data-test=outline-item]').find((i) => i.text().includes('Outskirt')).classes()).toContain('active');
    await w.get('[data-test=jump-back]').trigger('click');
    expect(w.get('[data-test=page]').text()).toContain('Welcome to the book.');
    expect(w.find('[data-test=jump-back]').exists()).toBe(false);
  });

  it('a link to a place deep in the book opens the outline down to it', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=kw]')[2].trigger('click');                                  // South Gate
    expect(w.get('[data-test=page]').text()).toContain('A wooden gate.');
    expect(w.findAll('[data-test=outline-item]').map((i) => i.text())).toContain('2a.South Gate');
    expect(w.findAll('[data-test=outline-item]').find((i) => i.text() === '2a.South Gate').classes()).toContain('active');
  });

  it('a link to a person opens the NPCs tab, and a page never links to itself', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=kw]')[0].trigger('click');                                  // Hardy
    await flushPromises();
    expect(w.get('[data-test=npc-page]').text()).toContain('A war veteran.');
    expect(w.get('[data-test=npc-page]').findAll('[data-test=kw]').map((a) => a.text())).toEqual(['South Gate']);   // "Hardy" is not a link on Hardy's own page
    await w.get('[data-test=jump-back]').trigger('click');
    await flushPromises();
    expect(w.find('[data-test=npcs]').exists()).toBe(false);
    expect(w.get('[data-test=page]').text()).toContain('Welcome to the book.');
  });

  it('the pictures of a node are shown with it: a map under its place, a portrait on the NPC page, and a click makes them big', async () => {
    const w = await mountIt();
    await w.findAll('[data-test=outline-item]')[1].trigger('click');
    await w.findAll('[data-test=outline-item]').find((i) => i.text() === 'Locations').trigger('click');
    await w.findAll('[data-test=outline-item]').find((i) => i.text() === '2a.South Gate').trigger('click');
    await flushPromises();
    const map = w.get('[data-test=page]').get('[data-test=picture]');
    expect(map.classes()).toContain('maps');
    expect(calls).toContain('/gm/adventures/mistyvale/image');
    await map.get('img').trigger('click');
    expect(document.body.querySelector('[data-test=lightbox]')).not.toBeNull();
    document.body.querySelector('[data-test=lightbox]').click();
    await flushPromises();
    expect(document.body.querySelector('[data-test=lightbox]')).toBeNull();
    await w.get('[data-test=story-npc]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=npc-page]').get('[data-test=picture]').classes()).toContain('people');
  });

  it('an NPC has a Notes box that saves what is written, a moment after the last key', async () => {
    vi.useFakeTimers();
    try {
      const w = await mountIt();
      await w.get('[data-test=tab-npcs]').trigger('click');
      await w.get('[data-test=npc-row]').trigger('click');
      await flushPromises();
      expect(w.get('[data-test=notes]').text()).toContain('Notes');
      await w.get('[data-test=note]').setValue('Owes the players a favor.');
      expect(saved).toEqual({});                                                  // not yet: it waits for the writing to stop
      await vi.advanceTimersByTimeAsync(900);
      await flushPromises();
      expect(saved).toEqual({ 'outskirt/locations/2a-south-gate/hardy': 'Owes the players a favor.' });
      expect(w.get('[data-test=note-status]').text()).toBe('Saved');
      expect(w.get('[data-test=has-note]').exists()).toBe(true);                  // the list marks the NPCs that have notes
    } finally {
      vi.useRealTimers();
    }
  });

  it('the notes come back when the adventure is opened again, and clearing the box removes the note', async () => {
    saved = { 'outskirt/locations/2a-south-gate/hardy': 'Old note.' };
    const w = await mountIt();
    await w.get('[data-test=tab-npcs]').trigger('click');
    expect(w.find('[data-test=has-note]').exists()).toBe(true);
    await w.get('[data-test=npc-row]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=note]').element.value).toBe('Old note.');
    await w.get('[data-test=note]').setValue('  ');
    await w.get('[data-test=note]').trigger('blur');                              // leaving the box saves at once
    await flushPromises();
    expect(saved).toEqual({});
  });

  it('says so when no adventure is loaded', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ adventures: [] }) })));
    const w = await mountIt();
    expect(w.get('[data-test=none]').text()).toContain('adventure.yaml');
    expect(w.find('[data-test=adventure-pick]').exists()).toBe(false);
  });
});
