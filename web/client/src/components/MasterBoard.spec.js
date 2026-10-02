import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import MasterBoard from './MasterBoard.vue';
import PinButton from './PinButton.vue';
import { useBoard } from '../board.js';

let stored;

beforeEach(() => {
  stored = { items: [], arrows: [], view: { x: 0, y: 0, zoom: 1 } };
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    if (opts.method === 'PUT') { stored = JSON.parse(opts.body); return { ok: true, status: 200, json: async () => ({ ok: true }) }; }
    return { ok: true, status: 200, json: async () => structuredClone(stored) };
  }));
});
afterEach(() => vi.unstubAllGlobals());

async function mountBoard() {
  const board = useBoard('gm');
  await board.load();
  const w = mount(MasterBoard, { global: { provide: { board } } });
  await flushPromises();
  return { w, board };
}

describe('MasterBoard', () => {
  it('starts empty and says how to fill it', async () => {
    const { w } = await mountBoard();
    expect(w.get('[data-test=empty]').text()).toContain('Pin things');
    expect(w.findAll('[data-test=item]')).toHaveLength(0);
  });

  it('writes notes: a button, a double-click, and what is typed is kept', async () => {
    const { w, board } = await mountBoard();
    await w.get('[data-test=add-note]').trigger('click');
    expect(w.findAll('[data-test=item]')).toHaveLength(1);
    await w.get('[data-test=note-text]').setValue('The ambush is at dawn');
    expect(board.items.value[0].text).toBe('The ambush is at dawn');
    await w.get('[data-test=canvas]').trigger('dblclick', { clientX: 300, clientY: 200 });
    expect(w.findAll('[data-test=item]')).toHaveLength(2);
    expect(w.find('[data-test=empty]').exists()).toBe(false);
  });

  it('shows what was pinned, with its title, and removes it', async () => {
    const { w, board } = await mountBoard();
    board.pin({ type: 'rule', key: 'core/rule/melee', title: 'Melee Combat', data: { body: 'Attacks within two meters.\n✦Parry: ends the attack.' } });
    board.pin({ type: 'table', key: 'table:Weather', title: 'Weather', data: { title: 'Weather', dice: 'D6', columns: ['WEATHER'], rows: [{ roll: '1', cells: ['Fog'] }] } });
    board.pin({ type: 'creature', key: 'c', title: 'Hardy', data: { blocks: [{ variant: '', fields: [{ label: 'HP', value: '16' }] }], attacks: [], abilities: [], intro: 'A veteran.' } });
    await flushPromises();
    const heads = w.findAll('[data-test=item-head]').map((h) => h.text());
    expect(heads.join(' ')).toContain('Melee Combat');
    expect(heads.join(' ')).toContain('Weather');
    expect(heads.join(' ')).toContain('Hardy');
    const text = w.text();
    expect(text).toContain('Attacks within two meters.');
    expect(text).toContain('Fog');
    expect(text).toContain('A veteran.');
    expect(text).toContain('16');
    await w.findAll('[data-test=item-remove]')[0].trigger('click');
    expect(board.items.value).toHaveLength(2);
  });

  it('an item is dragged by its top bar, and only moves what is dragged', async () => {
    const { w, board } = await mountBoard();
    const id = board.addNote();
    const other = board.addNote();
    const start = { x: board.items.value.find((i) => i.id === id).x, y: board.items.value.find((i) => i.id === id).y };
    const otherStart = board.items.value.find((i) => i.id === other).x;
    await flushPromises();
    const head = w.findAll('[data-test=item-head]')[0];
    await head.trigger('pointerdown', { clientX: 100, clientY: 100, button: 0, pointerId: 1 });
    await w.get('[data-test=canvas]').trigger('pointermove', { clientX: 160, clientY: 130, pointerId: 1 });
    await w.get('[data-test=canvas]').trigger('pointerup', { clientX: 160, clientY: 130, pointerId: 1 });
    const moved = board.items.value.find((i) => i.id === id);
    expect(moved.x).toBe(start.x + 60);
    expect(moved.y).toBe(start.y + 30);
    expect(board.items.value.find((i) => i.id === other).x).toBe(otherStart);
  });

  it('dragging the background moves the whole board, and the wheel and the buttons zoom it', async () => {
    const { w, board } = await mountBoard();
    const canvas = w.get('[data-test=canvas]');
    await canvas.trigger('pointerdown', { clientX: 50, clientY: 50, button: 0, pointerId: 1 });
    await canvas.trigger('pointermove', { clientX: 90, clientY: 80, pointerId: 1 });
    await canvas.trigger('pointerup', { clientX: 90, clientY: 80, pointerId: 1 });
    expect(board.view.value).toMatchObject({ x: 40, y: 30, zoom: 1 });
    await w.get('[data-test=zoom-in]').trigger('click');
    expect(w.get('[data-test=zoom-reset]').text()).toBe('125%');
    canvas.element.dispatchEvent(new WheelEvent('wheel', { deltaY: -200, clientX: 10, clientY: 10, bubbles: true, cancelable: true }));
    await flushPromises();
    expect(board.view.value.zoom).toBeGreaterThan(1.25);
    await w.get('[data-test=zoom-reset]').trigger('click');
    expect(w.get('[data-test=zoom-reset]').text()).toBe('100%');
  });

  it('arrows join two items, can be named, and deleted', async () => {
    const { w, board } = await mountBoard();
    const a = board.addNote();
    const b = board.addNote();
    board.link(a, b);
    await flushPromises();
    expect(w.findAll('[data-test=arrow]')).toHaveLength(1);
    expect(w.find('[data-test=arrow-tools]').exists()).toBe(false);
    await w.get('[data-test=arrow] .hit').trigger('pointerdown');
    await w.get('[data-test=arrow-label]').setValue('betrays');
    expect(board.arrows.value[0].label).toBe('betrays');
    expect(w.get('[data-test=arrow]').text()).toContain('betrays');
    await w.get('[data-test=arrow-delete]').trigger('click');
    expect(w.findAll('[data-test=arrow]')).toHaveLength(0);
    expect(board.arrows.value).toHaveLength(0);
  });

  it('draws an arrow by dragging the handle of an item onto another one', async () => {
    const { w, board } = await mountBoard();
    const a = board.addNote();
    const b = board.addNote();
    await flushPromises();
    const target = w.findAll('[data-test=item]')[1];
    const original = document.elementFromPoint;
    document.elementFromPoint = () => target.element;                                       // jsdom has no layout: say what is under the pointer
    try {
      await w.findAll('[data-test=link-handle]')[0].trigger('pointerdown', { clientX: 10, clientY: 10, button: 0, pointerId: 1 });
      await w.get('[data-test=canvas]').trigger('pointermove', { clientX: 200, clientY: 200, pointerId: 1 });
      await w.get('[data-test=canvas]').trigger('pointerup', { clientX: 200, clientY: 200, pointerId: 1 });
    } finally {
      document.elementFromPoint = original;
    }
    expect(board.arrows.value).toHaveLength(1);
    expect(board.arrows.value[0]).toMatchObject({ from: a, to: b });
  });

  it('Delete removes the selected item or arrow, but not while writing in a note', async () => {
    const { w, board } = await mountBoard();
    board.addNote();
    board.addNote();
    await flushPromises();
    await w.findAll('[data-test=item]')[0].trigger('pointerdown');
    await w.get('[data-test=note-text]').trigger('keydown', { key: 'Delete' });            // typing in the note: nothing is removed
    expect(board.items.value).toHaveLength(2);
    await w.get('[data-test=canvas]').trigger('keydown', { key: 'Delete' });
    expect(board.items.value).toHaveLength(1);
  });

  it('is as wide as the app and as tall as the window: the page does not scroll behind it, and full screen covers the tabs too (Esc comes back)', async () => {
    const { w } = await mountBoard();
    expect(document.documentElement.classList.contains('board-open')).toBe(true);
    expect(w.get('[data-test=board]').classes()).not.toContain('full');
    await w.get('[data-test=full]').trigger('click');
    expect(w.get('[data-test=board]').classes()).toContain('full');
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    await flushPromises();
    expect(w.get('[data-test=board]').classes()).not.toContain('full');
    w.unmount();
    expect(document.documentElement.classList.contains('board-open')).toBe(false);
  });

  it('says why when the board could not be read', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false, status: 500, json: async () => ({ error: 'broken' }) })));
    const board = useBoard('gm');
    await board.load();
    const w = mount(MasterBoard, { global: { provide: { board } } });
    expect(w.get('[data-test=board-error]').text()).toContain('broken');
    expect(w.find('[data-test=empty]').exists()).toBe(false);
  });
});

describe('PinButton', () => {
  it('pins what is open when it is clicked; players have no board, so no button', async () => {
    const board = useBoard('gm');
    await board.load();
    const snapshot = vi.fn(() => ({ type: 'rule', key: 'k', title: 'Parry', data: { body: 'x' } }));
    const w = mount(PinButton, { props: { snapshot }, global: { provide: { board } } });
    await w.get('[data-test=pin]').trigger('click');
    expect(snapshot).toHaveBeenCalledTimes(1);
    expect(board.items.value[0].pin.title).toBe('Parry');
    const players = mount(PinButton, { props: { snapshot } });
    expect(players.find('[data-test=pin]').exists()).toBe(false);
  });
});

describe('MasterBoard: pictures and a closed session', () => {
  const picture = () => new File([new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10])], 'loot.png', { type: 'image/png' });

  beforeEach(() => {
    URL.createObjectURL = vi.fn(() => 'blob:x');
    URL.revokeObjectURL = vi.fn();
  });

  it('a picture chosen with the button is kept on the server and put on the board', async () => {
    const posts = [];
    vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
      if (opts.method === 'POST') { posts.push(JSON.parse(opts.body)); return { ok: true, status: 201, json: async () => ({ id: 'i1.png' }) }; }
      if (opts.method === 'PUT') return { ok: true, status: 200, json: async () => ({}) };
      if (url.includes('/board/images/')) return { ok: true, status: 200, blob: async () => new Blob(['x']) };
      return { ok: true, status: 200, json: async () => ({ items: [], arrows: [], view: { x: 0, y: 0, zoom: 1 } }) };
    }));
    const { w, board } = await mountBoard();
    const input = w.get('[data-test=image-input]');
    Object.defineProperty(input.element, 'files', { value: [picture()], configurable: true });
    await input.trigger('change');
    await vi.waitFor(() => expect(board.items.value).toHaveLength(1));
    expect(posts).toHaveLength(1);
    expect(posts[0].image.length).toBeGreaterThan(5);
    expect(board.items.value[0]).toMatchObject({ kind: 'image', image: { id: 'i1.png' }, title: 'loot.png' });
    await flushPromises();
    expect(w.get('[data-test=item-head]').text()).toContain('loot.png');
  });

  it('a picture dropped on the canvas goes where it was dropped', async () => {
    vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
      if (opts.method === 'POST') return { ok: true, status: 201, json: async () => ({ id: 'i2.png' }) };
      if (opts.method === 'PUT') return { ok: true, status: 200, json: async () => ({}) };
      if (url.includes('/board/images/')) return { ok: true, status: 200, blob: async () => new Blob(['x']) };
      return { ok: true, status: 200, json: async () => ({ items: [], arrows: [], view: { x: 0, y: 0, zoom: 1 } }) };
    }));
    const { w, board } = await mountBoard();
    const drop = new Event('drop', { bubbles: true, cancelable: true });
    drop.dataTransfer = { files: [picture()] };
    drop.clientX = 500;
    drop.clientY = 300;
    w.get('[data-test=canvas]').element.dispatchEvent(drop);
    await vi.waitFor(() => expect(board.items.value).toHaveLength(1));
    expect(board.items.value[0].x).toBe(340);                                             // centred on the drop point (320 wide)
    expect(drop.defaultPrevented).toBe(true);
  });

  it('a closed session can be moved around and looked at, but nothing in it can be changed', async () => {
    const { w, board } = await mountBoard();
    const id = board.addNote('yellow', 'The ogre fell');
    await flushPromises();
    board.readOnly.value = true;
    await flushPromises();
    expect(w.get('[data-test=closed-note]').text()).toContain('Closed session');
    expect(w.find('[data-test=add-note]').exists()).toBe(false);
    expect(w.find('[data-test=add-image]').exists()).toBe(false);
    expect(w.find('[data-test=item-remove]').exists()).toBe(false);
    expect(w.find('[data-test=link-handle]').exists()).toBe(false);
    expect(w.find('[data-test=resize]').exists()).toBe(false);
    expect(w.get('[data-test=note-text]').attributes('readonly')).toBeDefined();
    await w.get('[data-test=item-head]').trigger('pointerdown', { clientX: 50, clientY: 50, button: 0, pointerId: 1 });
    await w.get('[data-test=canvas]').trigger('pointermove', { clientX: 200, clientY: 200, pointerId: 1 });
    await w.get('[data-test=canvas]').trigger('pointerup', { clientX: 200, clientY: 200, pointerId: 1 });
    expect(board.items.value.find((i) => i.id === id).x).toBe(board.items.value[0].x);   // it did not move
    await w.get('[data-test=zoom-in]').trigger('click');
    expect(w.get('[data-test=zoom-reset]').text()).toBe('125%');                           // but the view can still be changed
    await w.get('[data-test=canvas]').trigger('dblclick', { clientX: 300, clientY: 200 });
    expect(board.items.value).toHaveLength(1);
  });
});
