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

  it('armor and helmet are dropdowns of what the rules have for each slot, with None first', async () => {
    const cards = [
      { key: 'leather', name: 'Leather', slot: 'armor' },
      { key: 'chainmail', name: 'Chainmail', slot: 'armor' },
      { key: 'open-helmet', name: 'Open Helmet', slot: 'helmet' },
      { key: 'great-helm', name: 'Great Helm', slot: 'helmet' },
      { key: 'homebrew-coat', name: 'Coat' },
    ];
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ entries: cards }) })));
    const sheet = structuredClone(me);
    sheet.doc = { ...sheet.doc, armor: { key: 'leather', name: 'Leather' }, helmet: null };
    const w = mount(SheetView, { props: { me: sheet, token: 't' } });
    await flushPromises();
    const names = (sel) => w.findAll(`[data-test=${sel}] option`).map((o) => o.text());
    expect(names('armor-select')).toEqual(['None', 'Leather', 'Chainmail', 'Coat']);        // (a card with no slot is body armor)
    expect(names('helmet-select')).toEqual(['None', 'Open Helmet', 'Great Helm']);
    expect(w.get('[data-test=armor-select]').element.value).toBe('leather');
    expect(w.get('[data-test=helmet-select]').element.value).toBe('');
    await w.get('[data-test=helmet-select]').setValue('great-helm');
    await w.get('[data-test=armor-select]').setValue('');
    expect(w.vm.draft.helmet).toEqual({ key: 'great-helm', name: 'Great Helm' });
    expect(w.vm.draft.armor).toBeNull();
    expect(w.findAll('button').every((x) => x.text() !== 'Pick armor…' && x.text() !== 'Pick a helmet…')).toBe(true);          // no separate pickers
  });

  it('the GM can change the kin, the age and the profession of a sheet; a player cannot change kin or profession', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({
      ok: true,
      status: 200,
      json: async () => ({
        kins: [{ key: 'core/kin/human', title: 'Human' }, { key: 'core/kin/elf', title: 'Elf' }],
        professions: [{ key: 'core/profession/mage', title: 'Mage', schools: ['Animism', 'Elementalism'] }, { key: 'core/profession/bard', title: 'Bard', schools: [] }],
      }),
    })));
    const gm = structuredClone(me);
    gm.doc = { ...gm.doc, kin: { key: 'core/kin/human', name: 'Human' }, profession: { key: 'core/profession/bard', name: 'Bard' }, school: '' };
    const w = mount(SheetView, { props: { me: gm, token: 't', patchPath: '/gm/characters/c1' } });
    await w.findAll('button').find((b) => b.text() === 'Edit')?.trigger('click');
    await flushPromises();
    expect(w.findAll('[data-test=kin-select] option').map((o) => o.text())).toEqual(['Human', 'Elf']);
    expect(w.findAll('[data-test=age-select] option')).toHaveLength(3);                // the age is its own line, apart from the kin
    await w.get('[data-test=kin-select]').setValue('core/kin/elf');
    await w.get('[data-test=profession-select]').setValue('core/profession/mage');
    await flushPromises();
    expect(w.findAll('[data-test=school-select] option').map((o) => o.text())).toEqual(['Animism', 'Elementalism']);   // a mage picks a school
    const player = mount(SheetView, { props: { me: structuredClone(me), token: 't' } });
    await player.findAll('button').find((b) => b.text() === 'Edit')?.trigger('click');
    await flushPromises();
    expect(player.find('[data-test=kin-select]').exists()).toBe(false);
    expect(player.find('[data-test=profession-select]').exists()).toBe(false);
    expect(player.find('[data-test=age-select]').exists()).toBe(true);
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
