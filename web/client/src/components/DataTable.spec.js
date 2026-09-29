import { afterEach, describe, expect, it, vi } from 'vitest';
import { mount } from '@vue/test-utils';
import DataTable from './DataTable.vue';

const dice = {
  title: 'Odd Weather',
  dice: 'D6',
  columns: ['WEATHER'],
  rows: [{ roll: '1-3', cells: ['Fog'] }, { roll: '4', cells: ['Hail'] }, { roll: '5-6', cells: ['Snow'] }],
};
const plain = { title: 'Plain', dice: '', columns: ['A'], rows: [{ roll: '', cells: ['x'] }] };

afterEach(() => vi.restoreAllMocks());

describe('DataTable', () => {
  it('has no Roll button when the table has no die', () => {
    expect(mount(DataTable, { props: { table: plain } }).find('[data-test=table-roll]').exists()).toBe(false);
  });

  it('rolls its die and marks the row the roll lands on, ranges included', async () => {
    const w = mount(DataTable, { props: { table: dice } });
    expect(w.get('[data-test=table-roll]').text()).toBe('Roll D6');
    vi.spyOn(Math, 'random').mockReturnValue(0.5);                 // 1 + floor(0.5 * 6) = 4
    await w.get('[data-test=table-roll]').trigger('click');
    expect(w.get('[data-test=table-rolled]').text()).toBe('rolled 4');
    expect(w.findAll('tbody tr').map((r) => r.classes('picked'))).toEqual([false, true, false]);
    Math.random.mockReturnValue(0.99);                             // 6 -> the "5-6" row
    await w.get('[data-test=table-roll]').trigger('click');
    expect(w.findAll('tbody tr').map((r) => r.classes('picked'))).toEqual([false, false, true]);
  });

  it('lands on the row at that position when the rows have no roll texts', async () => {
    const t = { title: 'T', dice: 'D3', columns: ['A'], rows: [{ roll: '', cells: ['a'] }, { roll: '', cells: ['b'] }, { roll: '', cells: ['c'] }] };
    const w = mount(DataTable, { props: { table: t } });
    vi.spyOn(Math, 'random').mockReturnValue(0.4);                 // 1 + floor(1.2) = 2
    await w.get('[data-test=table-roll]').trigger('click');
    expect(w.findAll('tbody tr').map((r) => r.classes('picked'))).toEqual([false, true, false]);
  });
});
