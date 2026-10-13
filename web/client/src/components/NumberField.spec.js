import { describe, expect, it } from 'vitest';
import { mount } from '@vue/test-utils';
import NumberField from './NumberField.vue';

const write = async (w, text, event = 'input') => {
  w.element.value = text;
  await w.trigger(event);
};

describe('NumberField', () => {
  it('leaves what is typed alone, even an empty box, until the user is done', async () => {
    const w = mount(NumberField, { props: { modelValue: 12, min: 3, max: 18 } });
    await write(w, '');
    expect(w.element.value).toBe('');                                  // not put back to the minimum
    await write(w, '1');
    expect(w.element.value).toBe('1');
    expect(w.emitted('update:modelValue')).toBeUndefined();
  });

  it('checks the number when the box is left or Enter is pressed: kept between min and max', async () => {
    const w = mount(NumberField, { props: { modelValue: 12, min: 3, max: 18 } });
    await write(w, '1', 'change');
    expect(w.emitted('update:modelValue')[0]).toEqual([3]);
    expect(w.element.value).toBe('3');
    await write(w, '99');
    await w.trigger('blur');
    expect(w.emitted('update:modelValue')[1]).toEqual([18]);
    await write(w, '');
    await w.trigger('blur');
    expect(w.emitted('update:modelValue')[2]).toEqual([3]);
  });

  it('shows 0 as an empty box when it is asked to', () => {
    expect(mount(NumberField, { props: { modelValue: 0, blankZero: true } }).element.value).toBe('');
    expect(mount(NumberField, { props: { modelValue: 0 } }).element.value).toBe('0');
  });
});
