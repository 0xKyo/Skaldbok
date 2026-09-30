import { describe, expect, it } from 'vitest';
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
