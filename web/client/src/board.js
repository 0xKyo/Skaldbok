// A board of the Master tab: items (notes, things pinned from anywhere — a rule, a table, an NPC, a creature, a picture —) on a canvas, arrows between
// them, and where the canvas is looking at (x, y, zoom). The Screen is one; each Session is another, kept apart. A board is kept on the server a moment
// after every change; where exactly is up to its `source` (the Screen's own by default; a session's is given by sessions.js).
//
// The Screen lives above the tabs (GmView provides it) so that a "Pin" button anywhere in the app can add to it.
import { ref, watch } from 'vue';
import { ApiError, apiGet, apiSend } from './api.js';

export const NOTE_COLORS = ['yellow', 'green', 'blue', 'pink', 'white'];
const SAVE_DELAY_MS = 800;
const NOTICE_MS = 2600;
export const MIN_ZOOM = 0.2;
export const MAX_ZOOM = 2.5;

let counter = 0;
const newId = (prefix) => `${prefix}${Date.now().toString(36)}${(counter++).toString(36)}${Math.random().toString(36).slice(2, 5)}`;
const copy = (v) => JSON.parse(JSON.stringify(v));

export function useBoard(token, source = null) {
  const tokenNow = () => (typeof token === 'function' ? token() : token);
  const from = source ?? {
    read: () => apiGet('/gm/board', tokenNow()),
    write: (data) => apiSend('PUT', '/gm/board', tokenNow(), data),
  };

  const items = ref([]);                 // [{id, kind: 'note' | 'pin' | 'image', x, y, w, h, z, color?, text?, pin?: {type, key?, title, data}, image?: {...}}]
  const arrows = ref([]);                // [{id, from, to, label}]
  const view = ref({ x: 0, y: 0, zoom: 1 });
  const loaded = ref(false);             // saved only once it has been read: a board that could not be read is never overwritten
  const loadError = ref('');
  const readOnly = ref(false);           // a closed session: it can be looked at, not changed
  const status = ref('');                // '', 'Saving…', 'Saved', or what went wrong
  const notice = ref('');                // what the last pin did, for a moment
  const viewport = ref({ width: 900, height: 600 });     // the size of the canvas on the screen: new things appear in the middle of it
  let saveTimer = null;
  let noticeTimer = null;

  function say(text) {
    notice.value = text;
    clearTimeout(noticeTimer);
    noticeTimer = setTimeout(() => { notice.value = ''; }, NOTICE_MS);
  }

  async function load() {
    try {
      const data = await from.read();
      items.value = Array.isArray(data.items) ? data.items : [];
      arrows.value = Array.isArray(data.arrows) ? data.arrows : [];
      view.value = { x: 0, y: 0, zoom: 1, ...(data.view ?? {}) };
      loadError.value = '';
      loaded.value = true;
    } catch (e) {
      loadError.value = e instanceof ApiError ? e.message : 'Could not open the board.';
    }
  }

  async function save() {
    clearTimeout(saveTimer);
    if (!loaded.value || readOnly.value) return;
    status.value = 'Saving…';
    try {
      await from.write({ items: items.value, arrows: arrows.value, view: view.value });
      status.value = 'Saved';
    } catch (e) {
      status.value = e instanceof ApiError ? e.message : 'Could not save the board.';
    }
  }

  watch([items, arrows, view], () => {
    if (!loaded.value || readOnly.value) return;
    status.value = '';
    clearTimeout(saveTimer);
    saveTimer = setTimeout(save, SAVE_DELAY_MS);
  }, { deep: true });

  const topZ = () => items.value.reduce((z, i) => Math.max(z, i.z ?? 0), 0);

  // where a new thing goes: the middle of what is on screen, a little lower and to the right of the last one so they do not pile up
  function spot(w, h) {
    const { x, y, zoom } = view.value;
    let px = (viewport.value.width / 2 - x) / zoom - w / 2;
    let py = (viewport.value.height / 2 - y) / zoom - h / 2;
    while (items.value.some((i) => Math.abs(i.x - px) < 14 && Math.abs(i.y - py) < 14)) {
      px += 28;
      py += 28;
    }
    return { x: Math.round(px), y: Math.round(py) };
  }

  function bringToFront(id) {
    const item = items.value.find((i) => i.id === id);
    if (!item || readOnly.value) return;
    const top = topZ();
    if ((item.z ?? 0) < top || items.value.filter((i) => i.z === item.z).length > 1) item.z = top + 1;
  }

  // Something can only be added to a board that was read (or it would overwrite what it could not read) and that is open
  function canAdd() {
    if (!loaded.value) {
      say('The board is not available right now.');
      return false;
    }
    if (readOnly.value) {
      say('That session is closed: open it to change it.');
      return false;
    }
    return true;
  }

  function have(predicate) {
    return items.value.find(predicate);
  }

  // A picture on the board: {adventure, file} (one of the book's) or {id} (one the GM pasted or dropped), with the size to show it at.
  function addImage(image, { title = '', w = 320, h = 250 } = {}) {
    if (!canAdd()) return null;
    const key = image.id ? `board:${image.id}` : `adventure:${image.adventure}:${image.file}`;
    const same = have((i) => i.kind === 'image' && i.imageKey === key);
    if (same) {
      bringToFront(same.id);
      say(`“${title || 'That picture'}” is already on the board.`);
      return same.id;
    }
    const at = spot(w, h);
    const item = { id: newId('i'), kind: 'image', x: at.x, y: at.y, w, h, z: topZ() + 1, image, imageKey: key, title };
    items.value.push(item);
    say(`Pinned “${title || 'a picture'}” to the board.`);
    return item.id;
  }

  // Pins something from anywhere: {type: 'rule' | 'card' | 'table' | 'creature' | 'adventure' | 'image', key?, title, data}. The same thing twice is one pin.
  function pin({ type, key = '', title, data, w = 340, h = 300 }) {
    if (type === 'image') return addImage(data, { title, w: 320, h: 250 });
    if (!canAdd()) return null;
    if (key) {
      const already = have((i) => i.kind === 'pin' && i.pin?.type === type && i.pin?.key === key);
      if (already) {
        bringToFront(already.id);
        say(`“${title}” is already on the board.`);
        return already.id;
      }
    }
    const at = spot(w, h);
    const item = { id: newId('p'), kind: 'pin', x: at.x, y: at.y, w, h, z: topZ() + 1, pin: { type, key, title, data } };
    items.value.push(item);
    say(`Pinned “${title}” to the board.`);
    return item.id;
  }

  function addNote(color = 'yellow', text = '') {
    if (!canAdd()) return null;
    const at = spot(220, 160);
    const item = { id: newId('n'), kind: 'note', x: at.x, y: at.y, w: 220, h: 160, z: topZ() + 1, color, text };
    items.value.push(item);
    return item.id;
  }

  // What was sent from another board: a copy of it, of its own, in the middle of what is on screen
  function addCopy(original) {
    if (!canAdd()) return null;
    const at = spot(original.w, original.h);
    const item = { ...copy(original), id: newId(original.kind === 'note' ? 'n' : original.kind === 'image' ? 'i' : 'p'), x: at.x, y: at.y, z: topZ() + 1 };
    items.value.push(item);
    return item.id;
  }

  function remove(id) {
    if (readOnly.value) return;
    items.value = items.value.filter((i) => i.id !== id);
    arrows.value = arrows.value.filter((a) => a.from !== id && a.to !== id);
  }

  function link(fromId, to) {
    if (readOnly.value || !fromId || !to || fromId === to) return null;
    if (arrows.value.some((a) => (a.from === fromId && a.to === to))) return null;
    const arrow = { id: newId('a'), from: fromId, to, label: '' };
    arrows.value.push(arrow);
    return arrow.id;
  }

  const removeArrow = (id) => { if (!readOnly.value) arrows.value = arrows.value.filter((a) => a.id !== id); };

  function zoomAt(factor, cx, cy) {
    const { x, y, zoom } = view.value;
    const next = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom * factor));
    const k = next / zoom;
    view.value = { x: cx - (cx - x) * k, y: cy - (cy - y) * k, zoom: next };
  }

  // Everything on the screen: the zoom and place that show every item
  function fit() {
    if (!items.value.length) {
      view.value = { x: 0, y: 0, zoom: 1 };
      return;
    }
    const x0 = Math.min(...items.value.map((i) => i.x));
    const y0 = Math.min(...items.value.map((i) => i.y));
    const x1 = Math.max(...items.value.map((i) => i.x + i.w));
    const y1 = Math.max(...items.value.map((i) => i.y + i.h));
    const pad = 40;
    const zoom = Math.min(1, Math.max(MIN_ZOOM, Math.min((viewport.value.width - pad * 2) / (x1 - x0), (viewport.value.height - pad * 2) / (y1 - y0))));
    view.value = { x: Math.round((viewport.value.width - (x1 - x0) * zoom) / 2 - x0 * zoom), y: Math.round((viewport.value.height - (y1 - y0) * zoom) / 2 - y0 * zoom), zoom };
  }

  return { items, arrows, view, loaded, loadError, readOnly, status, notice, viewport, load, save, pin, addImage, addNote, addCopy, remove, link, removeArrow, bringToFront, zoomAt, fit, say };
}
