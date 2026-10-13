import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import InlineCard from './InlineCard.vue';
import { forgetCards } from '../lib/cards.js';

const spell = { key: 'core/spell/fireball', name: 'Fireball', subtitle: 'Elementalism · spell', fields: [{ label: 'Rank', value: '3' }], body: 'A ball of fire.' };
let asked;

beforeEach(() => {
  asked = [];
  forgetCards();
  vi.stubGlobal('fetch', vi.fn(async (url) => { asked.push(url); return { ok: true, status: 200, json: async () => ({ entries: [spell] }) }; }));
});
afterEach(() => vi.unstubAllGlobals());

const mountIt = (entry = { key: spell.key, name: 'Fireball', subtitle: spell.subtitle }) => mount(InlineCard, { props: { entry, type: 'spells', token: 't', prefix: '/gm' } });

describe('InlineCard', () => {
  it('is a line that opens into the card of the entry, where it is, and closes again', async () => {
    const w = mountIt();
    expect(w.find('[data-test=included-card]').exists()).toBe(false);
    expect(asked).toHaveLength(0);                                       // nothing is fetched until it is opened
    await w.get('[data-test=inline-card-toggle]').trigger('click');
    await flushPromises();
    expect(asked).toEqual(['/api/gm/content/spells']);
    expect(w.get('[data-test=included-card]').text()).toContain('A ball of fire.');
    expect(w.get('[data-test=included-card]').text()).toContain('Rank');
    await w.get('[data-test=inline-card-toggle]').trigger('click');
    expect(w.find('[data-test=included-card]').exists()).toBe(false);
  });

  it('fetches the list once for every card, and its name leads to the entry in the Reference', async () => {
    const a = mountIt();
    const b = mountIt();
    await a.get('[data-test=inline-card-toggle]').trigger('click');
    await b.get('[data-test=inline-card-toggle]').trigger('click');
    await flushPromises();
    expect(asked).toHaveLength(1);
    await a.get('.included-name').trigger('click');
    expect(a.emitted('follow')[0]).toEqual([spell.key]);
  });

  it('says so when the entry is no longer in the content', async () => {
    const w = mountIt({ key: 'core/spell/gone', name: 'Gone', subtitle: '' });
    await w.get('[data-test=inline-card-toggle]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=inline-card-missing]').text()).toContain('no longer');
  });
});
