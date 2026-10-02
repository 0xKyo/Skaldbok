import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { createNav } from './nav.js';

beforeEach(() => vi.useFakeTimers());
afterEach(() => vi.useRealTimers());

describe('createNav', () => {
  it('starts with nothing to go back or forward to', () => {
    const nav = createNav();
    expect(nav.canBack.value).toBe(false);
    expect(nav.canForward.value).toBe(false);
  });

  it('a piece that changes is a step; going back and forward gives the old pieces', () => {
    const nav = createNav();
    nav.record('tab', 'sheet');                              // where it starts: not a step
    nav.record('ref', null);
    expect(nav.canBack.value).toBe(false);
    nav.record('tab', 'rules');
    nav.record('ref', { type: 'spells' });
    expect(nav.slice('tab')).toBe('rules');
    expect(nav.canBack.value).toBe(true);
    nav.back();
    expect(nav.slice('ref')).toBeNull();                     // before the Reference page was opened
    expect(nav.slice('tab')).toBe('rules');
    nav.back();
    expect(nav.slice('tab')).toBe('sheet');
    expect(nav.canForward.value).toBe(true);
    nav.forward();
    nav.forward();
    expect(nav.slice('ref')).toEqual({ type: 'spells' });
    expect(nav.canForward.value).toBe(false);
  });

  it('what is as it was is not a step', () => {
    const nav = createNav();
    nav.record('adv', { page: 'a' });
    nav.record('adv', { page: 'b' });
    nav.record('adv', { page: 'b' });
    nav.back();
    expect(nav.canBack.value).toBe(false);
    nav.forward();
    expect(nav.canForward.value).toBe(false);
    nav.back();
    expect(nav.canBack.value).toBe(false);
  });

  it('putting a step back is not a step, and a new step after going back drops what was ahead', () => {
    const nav = createNav();
    nav.record('tab', 'zero');
    nav.record('tab', 'one');
    nav.record('tab', 'two');
    nav.back();
    expect(nav.restoring.value).toBe(true);
    nav.record('tab', 'three');                              // a part that reacts to the restore: ignored
    expect(nav.slice('tab')).toBe('one');
    vi.advanceTimersByTime(600);
    expect(nav.restoring.value).toBe(false);
    nav.record('tab', 'four');
    expect(nav.canForward.value).toBe(false);
    nav.back();
    expect(nav.slice('tab')).toBe('one');
  });

  it('keeps its own copy of what it was given', () => {
    const nav = createNav();
    const value = { page: 'a' };
    nav.record('adv', value);
    value.page = 'changed';
    expect(nav.slice('adv')).toEqual({ page: 'a' });
    nav.record('adv', { page: 'x' });
    nav.back();
    expect(nav.slice('adv')).toEqual({ page: 'a' });
  });

  it('tells who is listening that something was put back', () => {
    const nav = createNav();
    nav.record('tab', 'z');
    nav.record('tab', 'a');
    nav.record('tab', 'b');
    const before = nav.tick.value;
    nav.back();
    expect(nav.tick.value).toBe(before + 1);
  });
});
