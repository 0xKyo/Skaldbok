import { describe, expect, it } from 'vitest';
import { flushPromises } from '@vue/test-utils';
import { createNav } from '../nav.js';
import { mount } from '@vue/test-utils';
import { reactive } from 'vue';
import ReferenceView from './ReferenceView.vue';

const stubs = { ReferenceIndex: { template: '<nav class="index">index</nav>' }, ReferencePanel: { template: '<div class="panel">panel</div>' } };
const mountWith = (current) => mount(ReferenceView, { props: { state: reactive({ current }), token: 't' }, global: { stubs } });

describe('ReferenceView on a phone', () => {
  it('shows the index alone until a category is open, and then offers a way back to all of them', async () => {
    const w = mountWith(null);
    expect(w.find('.ref-view').classes()).not.toContain('is-open');
    expect(w.find('[data-test=reference-back]').exists()).toBe(false);
    const open = mountWith({ label: 'Creatures', tabs: [], tab: 0 });
    expect(open.find('.ref-view').classes()).toContain('is-open');
    await open.get('[data-test=reference-back]').trigger('click');
    expect(open.vm.state.current).toBeNull();
    expect(open.find('[data-test=reference-back]').exists()).toBe(false);
  });
});

describe('ReferenceView and the back / forward buttons', () => {
  it('the page open and the entry of its list are steps: going back opens them again', async () => {
    const nav = createNav();
    const state = reactive({ current: null });
    const w = mount(ReferenceView, { props: { state, token: 't' }, global: { stubs, provide: { nav } } });
    state.current = { mode: 'tables', type: 'spells', label: 'Spells', tabs: [], tab: 0, entryKey: null, seen: null };
    await flushPromises();
    state.current.seen = 'core/spell/fireball';                                    // the list opened an entry
    await flushPromises();
    state.current = { mode: 'tables', type: 'skills', label: 'Skills', tabs: [], tab: 0, entryKey: null, seen: null };
    await flushPromises();
    nav.back();
    await flushPromises();
    expect(state.current.type).toBe('spells');
    expect(state.current.entryKey).toBe('core/spell/fireball');                    // the entry is asked for again
    nav.back();
    await flushPromises();
    expect(state.current.seen).toBeNull();
    nav.back();
    await flushPromises();
    expect(state.current).toBeNull();                                             // before anything was open
    w.unmount();
  });
});
