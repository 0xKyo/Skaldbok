import { afterEach, describe, expect, it, vi } from 'vitest';
import { flushPromises } from '@vue/test-utils';
import { useReference } from './reference.js';

const content = {
  types: [
    { id: 'gear', label: 'Gear', count: 3 },
    { id: 'weapons', label: 'Weapons', count: 2 },
    { id: 'armor', label: 'Armor', count: 1 },
    { id: 'skills', label: 'Skills', count: 4 },
  ],
  rules: [{ key: 'skills', title: 'Skills', introOnly: true }, { key: 'world', title: 'World' }],
};

async function loaded() {
  vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => structuredClone(content) })));
  const ref = useReference('t');
  await ref.load();
  await flushPromises();
  return ref;
}

afterEach(() => vi.unstubAllGlobals());

describe('useReference', () => {
  it('folds Weapons and Armor into Gear as tabs and sums their counts', async () => {
    const ref = await loaded();
    expect(ref.items.map((i) => i.label)).toEqual(['Gear', 'Skills', 'World']);
    const gear = ref.items[0];
    expect(gear.tabs.map((t) => t.label)).toEqual(['General', 'Weapons', 'Armor']);
    expect(gear.count).toBe(6);
  });

  it('still merges a chapter titled like a type into two tabs', async () => {
    const ref = await loaded();
    expect(ref.items[1].tabs.map((t) => t.label)).toEqual(['All skills', 'General info']);
  });

  it('opens the right tab when a link points at a weapon', async () => {
    const ref = await loaded();
    ref.goTo({ key: 'core/weapon/axe', type: 'weapons' });
    expect(ref.current.label).toBe('Gear');
    expect(ref.current.tab).toBe(1);
    expect(ref.jump).toEqual({ type: 'weapons', key: 'core/weapon/axe' });
  });
});
