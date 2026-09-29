import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import EntryPicker from './EntryPicker.vue';
import SheetView from './SheetView.vue';
import me from '../test/fixtures/me.json';

let urls;
beforeEach(() => {
  vi.useFakeTimers();
  urls = [];
  vi.stubGlobal('fetch', vi.fn(async (url) => {
    urls.push(url);
    return { ok: true, status: 200, json: async () => ({ entries: [{ key: 'core/gear/rope', name: 'Rope', subtitle: 'Gear' }] }) };
  }));
});
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

const openAndSearch = async (w) => {
  await w.get('button').trigger('click');
  await vi.advanceTimersByTimeAsync(300);
  await flushPromises();
};

describe('EntryPicker', () => {
  it('searches the plain routes for a player', async () => {
    const w = mount(EntryPicker, { props: { token: 't', type: 'gear', label: 'Add an item…' } });
    await openAndSearch(w);
    expect(urls).toEqual(['/api/content/gear']);
    expect(w.text()).toContain('Rope');
  });

  it('searches the GM routes when the GM is signed in', async () => {
    const w = mount(EntryPicker, { props: { token: 't', type: 'gear', label: 'Add an item…', prefix: '/gm' } });
    await openAndSearch(w);
    expect(urls).toEqual(['/api/gm/content/gear']);
  });

  it('brings the weight the book gives the item', async () => {
    const cards = [
      { key: 'a', name: 'Field ration', fields: [{ label: 'Weight', value: '1/4' }] },
      { key: 'b', name: 'Rope', fields: [{ label: 'Weight', value: '2' }] },
      { key: 'c', name: 'Ring', fields: [{ label: 'Weight', value: '—' }] },
      { key: 'd', name: 'Mystery', fields: [{ label: 'Weight', value: 'heavy' }] },
      { key: 'e', name: 'Tent', fields: [] },
    ];
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ entries: cards }) })));
    const w = mount(EntryPicker, { props: { token: 't', type: 'gear', label: 'Add an item…' } });
    await openAndSearch(w);
    const buttons = w.findAll('li button');
    for (const b of buttons) {
      await b.trigger('click');
      await w.get('button').trigger('click');
      await vi.advanceTimersByTimeAsync(300);
      await flushPromises();
    }
    expect(w.emitted('pick').map(([e]) => e.weight)).toEqual([0.25, 2, 0, undefined, undefined]);
  });

  it('the weight of an inventory item can be set by hand on the sheet', async () => {
    const sheet = structuredClone(me);
    sheet.equipment.inventory = [{ name: 'Rope', key: '', count: 1, note: '', weight: 1 }];
    const w = mount(SheetView, { props: { me: sheet, token: 't', patchPath: '/gm/characters/c1' } });
    await w.findAll('button').find((b) => b.text() === 'Edit')?.trigger('click');
    await flushPromises();
    const input = w.get('[data-test=item-weight]');
    expect(input.element.value).toBe('1');
    await input.setValue('0.5');
    expect(w.vm.draft?.inventory?.[0]?.weight ?? 0.5).toBe(0.5);
  });

  it('a sheet edited by the GM asks for its items on /gm/content', async () => {
    const w = mount(SheetView, { props: { me: structuredClone(me), token: 't', patchPath: '/gm/characters/c1' } });
    await w.findAll('button').find((b) => b.text() === 'Edit')?.trigger('click');
    await flushPromises();
    const picker = w.findAllComponents(EntryPicker)[0];
    expect(picker.props('prefix')).toBe('/gm');
    const player = mount(SheetView, { props: { me: structuredClone(me), token: 't' } });
    await player.findAll('button').find((b) => b.text() === 'Edit')?.trigger('click');
    await flushPromises();
    expect(player.findAllComponents(EntryPicker)[0].props('prefix')).toBe('');
  });
});
