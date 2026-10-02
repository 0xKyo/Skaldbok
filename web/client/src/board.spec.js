import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { nextTick } from 'vue';
import { useBoard } from './board.js';

let stored;
let puts;

beforeEach(() => {
  vi.useFakeTimers();
  stored = { items: [], arrows: [], view: { x: 0, y: 0, zoom: 1 } };
  puts = [];
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    if (opts.method === 'PUT') {
      stored = JSON.parse(opts.body);
      puts.push(stored);
      return { ok: true, status: 200, json: async () => ({ ok: true }) };
    }
    return { ok: true, status: 200, json: async () => structuredClone(stored) };
  }));
});
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

const opened = async () => {
  const board = useBoard('gm');
  await board.load();
  return board;
};

describe('useBoard', () => {
  it('opens the board as it was saved', async () => {
    stored = { items: [{ id: 'n1', kind: 'note', x: 5, y: 6, w: 200, h: 100, text: 'hi' }], arrows: [], view: { x: 10, y: 20, zoom: 1.5 } };
    const board = await opened();
    expect(board.items.value).toHaveLength(1);
    expect(board.view.value).toEqual({ x: 10, y: 20, zoom: 1.5 });
    expect(board.loaded.value).toBe(true);
  });

  it('pins a thing in the middle of what is on screen, and says so', async () => {
    const board = await opened();
    board.viewport.value = { width: 1000, height: 600 };
    const id = board.pin({ type: 'rule', key: 'core/rule/melee', title: 'Melee Combat', data: { body: 'Text' } });
    const item = board.items.value.find((i) => i.id === id);
    expect(item).toMatchObject({ kind: 'pin', w: 340, h: 300, x: 330, y: 150 });
    expect(item.pin).toMatchObject({ type: 'rule', title: 'Melee Combat' });
    expect(board.notice.value).toContain('Pinned “Melee Combat”');
    vi.advanceTimersByTime(3000);
    expect(board.notice.value).toBe('');
  });

  it('pinning the same thing again does not make a copy, and new things do not pile up', async () => {
    const board = await opened();
    const first = board.pin({ type: 'table', key: 'table:Weather', title: 'Weather', data: {} });
    const again = board.pin({ type: 'table', key: 'table:Weather', title: 'Weather', data: {} });
    expect(again).toBe(first);
    expect(board.items.value).toHaveLength(1);
    expect(board.notice.value).toContain('already on the board');
    board.pin({ type: 'table', key: 'table:Other', title: 'Other', data: {} });
    const [a, b] = board.items.value;
    expect(a.x === b.x && a.y === b.y).toBe(false);
  });

  it('a note is written on the board; notes and pins are removed with the arrows that touch them', async () => {
    const board = await opened();
    const a = board.addNote('green', 'one');
    const b = board.addNote();
    const c = board.addNote();
    board.link(a, b);
    board.link(b, c);
    expect(board.items.value.find((i) => i.id === a)).toMatchObject({ kind: 'note', color: 'green', text: 'one' });
    board.remove(b);
    expect(board.items.value.map((i) => i.id)).toEqual([a, c]);
    expect(board.arrows.value).toHaveLength(0);
  });

  it('arrows join two different things once', async () => {
    const board = await opened();
    const a = board.addNote();
    const b = board.addNote();
    expect(board.link(a, a)).toBeNull();
    expect(board.link(a, b)).toBeTruthy();
    expect(board.link(a, b)).toBeNull();
    expect(board.link(b, a)).toBeTruthy();                 // the other way is another arrow
    expect(board.arrows.value).toHaveLength(2);
  });

  it('what is clicked comes to the front', async () => {
    const board = await opened();
    const a = board.addNote();
    const b = board.addNote();
    expect(board.items.value.find((i) => i.id === b).z).toBeGreaterThan(board.items.value.find((i) => i.id === a).z);
    board.bringToFront(a);
    expect(board.items.value.find((i) => i.id === a).z).toBeGreaterThan(board.items.value.find((i) => i.id === b).z);
  });

  it('zooms around a point and stays within its limits', async () => {
    const board = await opened();
    board.zoomAt(2, 100, 100);
    expect(board.view.value).toEqual({ x: -100, y: -100, zoom: 2 });   // the point under the cursor stays where it was
    board.zoomAt(100, 0, 0);
    expect(board.view.value.zoom).toBe(2.5);
    board.zoomAt(0.0001, 0, 0);
    expect(board.view.value.zoom).toBe(0.2);
  });

  it('fit shows everything', async () => {
    const board = await opened();
    board.viewport.value = { width: 800, height: 600 };
    const a = board.addNote();
    const b = board.addNote();
    board.items.value.find((i) => i.id === a).x = 0;
    board.items.value.find((i) => i.id === a).y = 0;
    board.items.value.find((i) => i.id === b).x = 2000;
    board.items.value.find((i) => i.id === b).y = 1500;
    board.fit();
    expect(board.view.value.zoom).toBeLessThan(0.5);
    expect(board.view.value.zoom).toBeGreaterThanOrEqual(0.2);
  });

  it('saves a moment after the last change, all in one request', async () => {
    const board = await opened();
    board.addNote('yellow', 'x');
    await nextTick();
    expect(puts).toHaveLength(0);                          // it waits for the changes to stop
    board.addNote('blue', 'y');
    await nextTick();
    await vi.advanceTimersByTimeAsync(900);
    expect(puts).toHaveLength(1);
    expect(puts[0].items).toHaveLength(2);
    expect(board.status.value).toBe('Saved');
  });

  it('never saves a board it could not read', async () => {
    vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
      if (opts.method === 'PUT') { puts.push(1); return { ok: true, status: 200, json: async () => ({}) }; }
      return { ok: false, status: 500, json: async () => ({ error: 'broken' }) };
    }));
    const board = useBoard('gm');
    await board.load();
    expect(board.loaded.value).toBe(false);
    expect(board.loadError.value).toBe('broken');
    expect(board.pin({ type: 'rule', key: 'k', title: 'T', data: {} })).toBeNull();
    board.items.value.push({ id: 'x', kind: 'note', x: 0, y: 0, w: 10, h: 10 });
    await nextTick();
    await vi.advanceTimersByTimeAsync(2000);
    expect(puts).toHaveLength(0);
  });
});

describe('useBoard: pictures, copies and a closed board', () => {
  it('a picture of the book goes on the board once, with its size', async () => {
    const board = await opened();
    const id = board.addImage({ adventure: 'mistyvale', file: 'images/maps/outskirt.jpg' }, { title: 'Outskirt', w: 400, h: 300 });
    expect(board.items.value[0]).toMatchObject({ id, kind: 'image', w: 400, h: 300, title: 'Outskirt', image: { adventure: 'mistyvale', file: 'images/maps/outskirt.jpg' } });
    expect(board.pin({ type: 'image', title: 'Outskirt', data: { adventure: 'mistyvale', file: 'images/maps/outskirt.jpg' } })).toBe(id);
    expect(board.items.value).toHaveLength(1);
    expect(board.notice.value).toContain('already on the board');
  });

  it('a picture the GM brought is another thing, kept by its id', async () => {
    const board = await opened();
    board.addImage({ id: 'i1.png' }, { title: 'loot.png' });
    board.addImage({ id: 'i2.png' }, { title: 'monster.png' });
    expect(board.items.value.map((i) => i.image.id)).toEqual(['i1.png', 'i2.png']);
  });

  it('a copy is a new item of its own, in the middle of what is on screen', async () => {
    const board = await opened();
    board.viewport.value = { width: 1000, height: 600 };
    const original = { id: 'n9', kind: 'note', x: 5000, y: 5000, w: 220, h: 160, z: 3, color: 'blue', text: 'The loot' };
    const id = board.addCopy(original);
    const copy = board.items.value.find((i) => i.id === id);
    expect(copy).toMatchObject({ kind: 'note', color: 'blue', text: 'The loot', x: 390, y: 220 });
    expect(id).not.toBe('n9');
    copy.text = 'changed';
    expect(original.text).toBe('The loot');
  });

  it('a closed board cannot be changed or saved, and says why', async () => {
    const board = await opened();
    const a = board.addNote();
    const b = board.addNote();
    board.readOnly.value = true;
    expect(board.addNote()).toBeNull();
    expect(board.notice.value).toContain('closed');
    expect(board.pin({ type: 'rule', key: 'k', title: 'T', data: {} })).toBeNull();
    expect(board.addCopy({ id: 'x', kind: 'note', x: 0, y: 0, w: 10, h: 10 })).toBeNull();
    board.remove(a);
    expect(board.link(a, b)).toBeNull();
    expect(board.items.value).toHaveLength(2);
    puts.length = 0;
    await board.save();
    expect(puts).toHaveLength(0);
  });
});
