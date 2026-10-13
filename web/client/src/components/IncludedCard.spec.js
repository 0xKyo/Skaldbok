import { describe, expect, it } from 'vitest';
import { mount } from '@vue/test-utils';
import IncludedCard from './IncludedCard.vue';

const card = { key: 'core/ability/adaptive', name: 'Adaptive', subtitle: 'Innate ability', fields: [{ label: 'WP', value: '3' }], body: 'Choose another skill.' };

describe('IncludedCard (the include pattern)', () => {
  it('shows the card open, closes and opens again, and its name leads to the entry', async () => {
    const w = mount(IncludedCard, { props: { card } });
    expect(w.text()).toContain('Choose another skill.');
    await w.get('[data-test=included-toggle]').trigger('click');
    expect(w.text()).not.toContain('Choose another skill.');
    expect(w.text()).toContain('Adaptive');                                  // the name stays: it is what is left to open
    await w.get('[data-test=included-toggle]').trigger('click');
    expect(w.text()).toContain('Choose another skill.');
    await w.get('.included-name').trigger('click');
    expect(w.emitted('follow')[0]).toEqual([card.key]);
  });

  it('can start closed, and has no toggle where something else opens it', () => {
    expect(mount(IncludedCard, { props: { card, startOpen: false } }).text()).not.toContain('Choose another skill.');
    expect(mount(IncludedCard, { props: { card, closable: false } }).find('[data-test=included-toggle]').exists()).toBe(false);
  });
});
