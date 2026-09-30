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
