<script setup>
// A board of the Master tab (the Screen, or a session): a lightweight canvas. You can move around it (drag the background) and zoom (wheel, or two
// fingers), with notes you write, things pinned from anywhere in the app (📌), pictures (pasted, dropped or pinned from an adventure), and arrows you
// draw from one to another (drag the ➜ at the edge of an item onto another one). Drag an item by its top bar, resize it by its corner. On the Screen,
// the → on an item sends a copy of it to the active session. A closed session can be looked at but not changed.
import { computed, inject, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import BoardImage from './BoardImage.vue';
import BoardPinBody from './BoardPinBody.vue';
import { MAX_ZOOM, MIN_ZOOM, NOTE_COLORS } from '../board.js';
import { pictureForBoard } from '../lib/pictures.js';
import { apiSend } from '../api.js';

const props = defineProps({
  board: { type: Object, default: null },         // the board to show (the Screen's, if not given)
  token: { type: String, default: '' },
  inSession: { type: Boolean, default: false },   // a session's board: nothing is sent from it
});

const board = props.board ?? inject('board');
const sessions = inject('sessions', null);
const campaigns = inject('campaigns', null);

const root = ref(null);
const slot = ref(null);               // an empty place in the page: where the board starts (right under the tabs)
const fileInput = ref(null);
const height = ref(600);              // as tall as the window allows, from under the tabs to the bottom; the width is the app's own
const full = ref(false);              // the board over everything, tabs included (Esc comes back)
const selectedItem = ref('');
const selectedArrow = ref('');
const linking = ref(null);            // {from, x, y}: an arrow being drawn, until the pointer is let go
let drag = null;                      // what the pointer is doing: {kind: 'pan' | 'item' | 'resize' | 'link', ...}
const pointers = new Map();           // the fingers on the background, for pinching
let pinch = null;
let observer = null;

const ICONS = { rule: '📖', card: '🃏', table: '▦', creature: '🐾', adventure: '🗺' };
const ro = computed(() => board.readOnly.value);                  // a closed session
const canSend = computed(() => !!sessions && !props.inSession);

// ---- what is drawn ----
const sorted = computed(() => [...board.items.value].sort((a, b) => (a.z ?? 0) - (b.z ?? 0)));
const worldStyle = computed(() => ({ transform: `translate(${board.view.value.x}px, ${board.view.value.y}px) scale(${board.view.value.zoom})` }));
const gridStyle = computed(() => {
  const { x, y, zoom } = board.view.value;
  const size = Math.max(8, 28 * zoom);
  return { backgroundSize: `${size}px ${size}px`, backgroundPosition: `${x}px ${y}px` };
});
const percent = computed(() => `${Math.round(board.view.value.zoom * 100)}%`);

const itemOf = (id) => board.items.value.find((i) => i.id === id);
const centerOf = (i) => ({ x: i.x + i.w / 2, y: i.y + i.h / 2 });

// where the line from the centre of a box towards a point leaves the box
function edge(i, toward) {
  const c = centerOf(i);
  const dx = toward.x - c.x;
  const dy = toward.y - c.y;
  if (!dx && !dy) return c;
  const t = Math.min(dx ? i.w / 2 / Math.abs(dx) : Infinity, dy ? i.h / 2 / Math.abs(dy) : Infinity);
  return { x: c.x + dx * t, y: c.y + dy * t };
}

const lines = computed(() => board.arrows.value.flatMap((a) => {
  const from = itemOf(a.from);
  const to = itemOf(a.to);
  if (!from || !to) return [];
  const p1 = edge(from, centerOf(to));
  const p2 = edge(to, centerOf(from));
  return [{ id: a.id, label: a.label, x1: p1.x, y1: p1.y, x2: p2.x, y2: p2.y, mx: (p1.x + p2.x) / 2, my: (p1.y + p2.y) / 2 }];
}));

const arrowSelected = computed(() => board.arrows.value.find((a) => a.id === selectedArrow.value) ?? null);
const tempLine = computed(() => {
  if (!linking.value) return null;
  const from = itemOf(linking.value.from);
  if (!from) return null;
  const p1 = edge(from, linking.value);
  return { x1: p1.x, y1: p1.y, x2: linking.value.x, y2: linking.value.y };
});

// ---- the canvas on the screen ----
function measure() {
  if (!root.value) return;
  const r = root.value.getBoundingClientRect();
  board.viewport.value = { width: r.width, height: r.height };
}

// The board is as wide as the rest of the app and as tall as the window allows (the page itself does not scroll while it is open); Full screen
// is the one that takes the whole window.
function place() {
  if (slot.value) height.value = Math.max(320, Math.round(window.innerHeight - slot.value.getBoundingClientRect().top - 14));
  measure();
}
function onEsc(e) {
  if (e.key === 'Escape' && full.value) full.value = false;
}
async function toggleFull() {
  full.value = !full.value;
  await nextTick();
  measure();
}

// A picture pasted anywhere on the page (while no text is being written) goes on the board
function onPaste(e) {
  if (ro.value || e.target?.tagName === 'TEXTAREA' || e.target?.tagName === 'INPUT') return;
  const file = [...(e.clipboardData?.items ?? [])].map((i) => (i.kind === 'file' ? i.getAsFile() : null)).find((f) => f && String(f.type).startsWith('image/'));
  if (file) {
    e.preventDefault();
    addPicture(file);
  }
}

onMounted(() => {
  document.documentElement.scrollTop = 0;
  document.documentElement.classList.add('board-open');
  place();
  window.addEventListener('resize', place);
  window.addEventListener('keydown', onEsc);
  window.addEventListener('paste', onPaste);
  measure();
  if (typeof ResizeObserver !== 'undefined') {
    observer = new ResizeObserver(measure);
    observer.observe(root.value);
  }
});
onBeforeUnmount(() => {
  document.documentElement.classList.remove('board-open');
  window.removeEventListener('resize', place);
  window.removeEventListener('keydown', onEsc);
  window.removeEventListener('paste', onPaste);
  if (observer) observer.disconnect();
  board.save();                                          // what was done a moment ago is not lost by leaving the tab
});

function toWorld(clientX, clientY) {
  const r = root.value.getBoundingClientRect();
  const { x, y, zoom } = board.view.value;
  return { x: (clientX - r.left - x) / zoom, y: (clientY - r.top - y) / zoom };
}
const local = (e) => {
  const r = root.value.getBoundingClientRect();
  return { x: e.clientX - r.left, y: e.clientY - r.top };
};

// ---- the pointer ----
function onDown(e) {                                      // the background: move the canvas (two fingers: zoom it)
  if (e.button !== undefined && e.button !== 0) return;
  selectedItem.value = '';
  selectedArrow.value = '';
  root.value.focus({ preventScroll: true });
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  root.value.setPointerCapture?.(e.pointerId);
  if (pointers.size === 2) {
    drag = null;
    const [a, b] = [...pointers.values()];
    pinch = { dist: Math.hypot(a.x - b.x, a.y - b.y) };
    return;
  }
  drag = { kind: 'pan', cx: e.clientX, cy: e.clientY, vx: board.view.value.x, vy: board.view.value.y };
}

function startDrag(e, item) {                             // an item by its top bar
  if (e.button !== undefined && e.button !== 0) return;
  selectedItem.value = item.id;
  selectedArrow.value = '';
  if (ro.value) return;
  board.bringToFront(item.id);
  drag = { kind: 'item', id: item.id, cx: e.clientX, cy: e.clientY, ix: item.x, iy: item.y };
  root.value.setPointerCapture?.(e.pointerId);
}

function startResize(e, item) {
  if (ro.value) return;
  selectedItem.value = item.id;
  drag = { kind: 'resize', id: item.id, cx: e.clientX, cy: e.clientY, w: item.w, h: item.h };
  root.value.setPointerCapture?.(e.pointerId);
}

function startLink(e, item) {
  if (ro.value) return;
  selectedItem.value = item.id;
  const at = toWorld(e.clientX, e.clientY);
  linking.value = { from: item.id, x: at.x, y: at.y };
  drag = { kind: 'link' };
  root.value.setPointerCapture?.(e.pointerId);
}

function onMove(e) {
  if (pointers.has(e.pointerId)) pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  if (pinch && pointers.size >= 2) {
    const [a, b] = [...pointers.values()];
    const dist = Math.hypot(a.x - b.x, a.y - b.y);
    if (pinch.dist > 0 && dist > 0) {
      const r = root.value.getBoundingClientRect();
      board.zoomAt(dist / pinch.dist, (a.x + b.x) / 2 - r.left, (a.y + b.y) / 2 - r.top);
    }
    pinch.dist = dist;
    return;
  }
  if (!drag) return;
  const zoom = board.view.value.zoom;
  if (drag.kind === 'pan') {
    board.view.value = { ...board.view.value, x: drag.vx + (e.clientX - drag.cx), y: drag.vy + (e.clientY - drag.cy) };
  } else if (drag.kind === 'item') {
    const item = itemOf(drag.id);
    if (item) {
      item.x = Math.round(drag.ix + (e.clientX - drag.cx) / zoom);
      item.y = Math.round(drag.iy + (e.clientY - drag.cy) / zoom);
    }
  } else if (drag.kind === 'resize') {
    const item = itemOf(drag.id);
    if (item) {
      item.w = Math.max(150, Math.round(drag.w + (e.clientX - drag.cx) / zoom));
      item.h = Math.max(90, Math.round(drag.h + (e.clientY - drag.cy) / zoom));
    }
  } else if (drag.kind === 'link' && linking.value) {
    const at = toWorld(e.clientX, e.clientY);
    linking.value = { ...linking.value, x: at.x, y: at.y };
  }
}

function onUp(e) {
  pointers.delete(e.pointerId);
  if (pointers.size < 2) pinch = null;
  if (drag?.kind === 'link' && linking.value) {                    // let go over an item: an arrow to it
    const over = document.elementFromPoint?.(e.clientX, e.clientY)?.closest?.('[data-item-id]');
    const to = over?.getAttribute('data-item-id');
    if (to) {
      const id = board.link(linking.value.from, to);
      if (id) selectedArrow.value = id;
    }
    linking.value = null;
  }
  drag = null;
}

function onWheel(e) {
  const body = e.target?.closest?.('.item-body');                  // a long pinned text scrolls; the rest of the board zooms
  if (body && body.scrollHeight > body.clientHeight + 1 && !e.ctrlKey) return;
  e.preventDefault();
  const at = local(e);
  board.zoomAt(Math.exp(-e.deltaY * 0.0015), at.x, at.y);
}

function onDoubleClick(e) {                                       // a note where you double-click
  if (ro.value) return;
  if (e.target !== root.value && !e.target.classList?.contains('world')) return;
  const id = board.addNote('yellow');
  if (!id) return;
  const at = toWorld(e.clientX, e.clientY);
  const item = itemOf(id);
  item.x = Math.round(at.x - item.w / 2);
  item.y = Math.round(at.y - 14);
  selectedItem.value = id;
}

function onKey(e) {
  if (e.target?.tagName === 'TEXTAREA' || e.target?.tagName === 'INPUT') return;
  if (e.key !== 'Delete' && e.key !== 'Backspace') return;
  if (selectedArrow.value) {
    board.removeArrow(selectedArrow.value);
    selectedArrow.value = '';
  } else if (selectedItem.value) {
    board.remove(selectedItem.value);
    selectedItem.value = '';
  }
}

// ---- pictures: pasted, dropped, or chosen with the button ----
async function addPicture(file, at = null) {
  try {
    const { base64, width, height } = await pictureForBoard(file);
    const kept = await apiSend('POST', '/gm/board/images', props.token, { image: base64 });
    const w = 320;
    const h = width && height ? Math.max(120, Math.round((w * height) / width) + 28) : 250;
    const id = board.addImage({ id: kept.id }, { title: file.name || 'a picture', w, h });
    if (id && at) {
      const item = itemOf(id);
      item.x = Math.round(at.x - w / 2);
      item.y = Math.round(at.y - 14);
    }
    if (id) selectedItem.value = id;
  } catch (e) {
    board.say(e?.message || 'Could not add the picture.');
  }
}

function onDrop(e) {
  const file = [...(e.dataTransfer?.files ?? [])].find((f) => String(f.type).startsWith('image/'));
  if (!file || ro.value) return;
  e.preventDefault();
  addPicture(file, toWorld(e.clientX, e.clientY));
}

function onFileChosen(e) {
  const file = e.target.files?.[0];
  e.target.value = '';
  if (file) addPicture(file);
}

// ---- the buttons ----
const zoomBy = (factor) => board.zoomAt(factor, board.viewport.value.width / 2, board.viewport.value.height / 2);
const resetZoom = () => zoomBy(1 / board.view.value.zoom);
function addNote(color) {
  selectedItem.value = board.addNote(color) ?? '';
}
function removeItem(item) {
  board.remove(item.id);
  if (selectedItem.value === item.id) selectedItem.value = '';
}
function pickArrow(id) {
  selectedArrow.value = id;
  selectedItem.value = '';
}
const sendToSession = (item) => sessions.send(item);
const titleOf = (item) => (item.kind === 'pin' ? item.pin.title : item.kind === 'image' ? item.title || 'Picture' : 'Note');
const iconOf = (item) => (item.kind === 'pin' ? ICONS[item.pin.type] ?? '📌' : item.kind === 'image' ? '🖼' : '');
</script>

<template>
  <div ref="slot" class="board-slot"></div>
  <section class="board-full" :class="{ full }" :style="full ? {} : { height: `${height}px` }" data-test="board">
    <p v-if="board.loadError.value" class="error board-error" data-test="board-error">
      {{ board.loadError.value }} <button type="button" class="tool" @click="board.load()">Try again</button>
    </p>

    <div ref="root" class="canvas" :class="{ closed: ro }" :style="gridStyle" tabindex="0" data-test="canvas"
         @pointerdown="onDown" @pointermove="onMove" @pointerup="onUp" @pointercancel="onUp" @wheel="onWheel" @dblclick="onDoubleClick" @keydown="onKey"
         @dragover.prevent @drop="onDrop">
      <div class="toolbar" data-test="toolbar" @pointerdown.stop @dblclick.stop>
        <span v-if="!ro" class="tool-group">
          <button type="button" class="tool" data-test="add-note" @click="addNote('yellow')">+ Note</button>
          <button v-for="c in NOTE_COLORS.slice(1, 4)" :key="c" type="button" class="dot" :class="c" :title="`A ${c} note`" :aria-label="`A ${c} note`" @click="addNote(c)"></button>
          <button type="button" class="tool" title="Add a picture (or paste or drop one)" data-test="add-image" @click="fileInput.click()">+ Image</button>
          <input ref="fileInput" type="file" accept="image/*" hidden data-test="image-input" @change="onFileChosen" />
        </span>
        <span class="tool-group">
          <button type="button" class="tool round" aria-label="Zoom out" title="Zoom out" data-test="zoom-out" :disabled="board.view.value.zoom <= MIN_ZOOM" @click="zoomBy(1 / 1.25)">−</button>
          <button type="button" class="tool" title="Back to 100%" data-test="zoom-reset" @click="resetZoom">{{ percent }}</button>
          <button type="button" class="tool round" aria-label="Zoom in" title="Zoom in" data-test="zoom-in" :disabled="board.view.value.zoom >= MAX_ZOOM" @click="zoomBy(1.25)">+</button>
          <button type="button" class="tool" title="Show everything" data-test="fit" @click="board.fit()">Fit</button>
        </span>
        <span v-if="arrowSelected && !ro" class="tool-group" data-test="arrow-tools">
          <input v-model="arrowSelected.label" class="label-input" placeholder="Name the arrow…" maxlength="60" aria-label="Arrow label" data-test="arrow-label" />
          <button type="button" class="tool danger" data-test="arrow-delete" @click="board.removeArrow(arrowSelected.id); selectedArrow = ''">Delete arrow</button>
        </span>
        <span class="tool-group">
          <button type="button" class="tool" :title="full ? 'Back to the app (Esc)' : 'The board over the whole window'" data-test="full" @click="toggleFull">{{ full ? '⤡ Exit full screen' : '⤢ Full screen' }}</button>
        </span>
        <span v-if="canSend" class="session-chip" :class="{ none: !sessions.active.value }" data-test="session-chip">
          {{ sessions.activeSession() ? `→ ${campaigns?.find(sessions.activeSession().campaign)?.name ?? 'Campaign'} · ${sessions.activeSession().name}` : 'No active session' }}
        </span>
        <span v-if="ro" class="session-chip closed" data-test="closed-note">Closed session: open it to change it</span>
        <span v-if="board.status.value" class="status" data-test="board-status">{{ board.status.value }}</span>
      </div>

      <div class="world" :style="worldStyle">
        <svg class="links" width="1" height="1" aria-hidden="true">
          <defs>
            <marker id="board-arrowhead" markerWidth="10" markerHeight="8" refX="9" refY="4" orient="auto" markerUnits="userSpaceOnUse">
              <path d="M0,0 L10,4 L0,8 Z" class="head" />
            </marker>
          </defs>
          <g v-for="l in lines" :key="l.id" class="link" :class="{ selected: selectedArrow === l.id }" data-test="arrow">
            <line :x1="l.x1" :y1="l.y1" :x2="l.x2" :y2="l.y2" class="hit" @pointerdown.stop="pickArrow(l.id)" />
            <line :x1="l.x1" :y1="l.y1" :x2="l.x2" :y2="l.y2" class="stroke" marker-end="url(#board-arrowhead)" />
            <text v-if="l.label" :x="l.mx" :y="l.my - 6" text-anchor="middle" class="label">{{ l.label }}</text>
          </g>
          <line v-if="tempLine" :x1="tempLine.x1" :y1="tempLine.y1" :x2="tempLine.x2" :y2="tempLine.y2" class="stroke temp" marker-end="url(#board-arrowhead)" />
        </svg>

        <div v-for="item in sorted" :key="item.id" class="item" :class="[item.kind, item.color, { selected: selectedItem === item.id }]" data-test="item"
             :data-item-id="item.id" :style="{ left: `${item.x}px`, top: `${item.y}px`, width: `${item.w}px`, height: `${item.h}px`, zIndex: item.z ?? 0 }"
             @pointerdown.stop="selectedItem = item.id; selectedArrow = ''; board.bringToFront(item.id)">
          <div class="item-head" :class="{ fixed: ro }" data-test="item-head" @pointerdown="startDrag($event, item)">
            <span class="title" :title="titleOf(item)">{{ iconOf(item) }} {{ titleOf(item) }}</span>
            <button v-if="canSend" type="button" class="send" :disabled="!sessions.active.value" :title="sessions.active.value ? 'Send a copy to the active session' : 'There is no active session'"
                    aria-label="Send to the active session" data-test="send" @pointerdown.stop @click="sendToSession(item)">→</button>
            <button v-if="!ro" type="button" class="close" aria-label="Remove from the board" title="Remove" data-test="item-remove" @pointerdown.stop @click="removeItem(item)">×</button>
          </div>
          <div class="item-body" @pointerdown.stop>
            <textarea v-if="item.kind === 'note'" v-model="item.text" class="note-text" :readonly="ro" placeholder="Write here…" aria-label="Note" data-test="note-text"></textarea>
            <BoardImage v-else-if="item.kind === 'image'" :token="token" :image="item.image" :alt="item.title" />
            <BoardPinBody v-else :pin="item.pin" />
          </div>
          <span v-if="!ro" class="link-handle" title="Drag onto another item to draw an arrow" data-test="link-handle" @pointerdown.stop.prevent="startLink($event, item)">➜</span>
          <span v-if="!ro" class="resize" data-test="resize" @pointerdown.stop.prevent="startResize($event, item)"></span>
        </div>
      </div>

      <p v-if="!board.items.value.length && board.loaded.value" class="empty" data-test="empty">
        <template v-if="ro"><b>This session is empty.</b></template>
        <template v-else>
          <b>{{ inSession ? 'This session is empty.' : 'Your board is empty.' }}</b><br />
          Pin things with the 📌 button (rules, tables, NPCs, creatures, adventure pages, pictures), add notes with “+ Note” or by double-clicking, paste or drop a picture, and join them with arrows.
        </template>
      </p>
    </div>
  </section>
</template>

<style scoped>
.board-slot { height: 0; }
.board-full { position: relative; display: flex; flex-direction: column; background: #efe6cf; border: 1px solid var(--line); border-radius: 10px; overflow: hidden; }
.board-full.full { position: fixed; inset: 0; z-index: 8000; height: auto; border: 0; border-radius: 0; }
.board-error { padding: 4px 16px; margin: 0; }
.canvas {
  position: relative; overflow: hidden; flex: 1; min-height: 0; touch-action: none; outline: none; user-select: none;
  background-color: #efe6cf; background-image: radial-gradient(circle, rgba(90, 70, 40, 0.28) 1.2px, transparent 1.4px);
  cursor: grab;
}
.canvas.closed { background-color: #e6dfd0; }
.canvas:active { cursor: grabbing; }
.world { position: absolute; left: 0; top: 0; width: 0; height: 0; transform-origin: 0 0; }
.links { position: absolute; left: 0; top: 0; overflow: visible; pointer-events: none; z-index: 0; }
.link .hit { stroke: transparent; stroke-width: 16; pointer-events: stroke; cursor: pointer; }
.link .stroke, .stroke.temp { stroke: var(--green-dark); stroke-width: 2.4; pointer-events: none; }
.stroke.temp { stroke-dasharray: 6 5; }
.link.selected .stroke { stroke: var(--red); stroke-width: 3.4; }
.head { fill: var(--green-dark); }
.label { font-size: 13px; fill: var(--ink); paint-order: stroke; stroke: #efe6cf; stroke-width: 4px; }

.toolbar { position: absolute; z-index: 5000; top: 10px; left: 10px; right: 10px; display: flex; flex-wrap: wrap; align-items: center; gap: 8px; pointer-events: none; }
.toolbar > * { pointer-events: auto; }
.tool-group { display: inline-flex; align-items: center; gap: 4px; padding: 3px 6px; background: rgba(251, 246, 230, 0.94); border: 1px solid var(--line); border-radius: 22px; box-shadow: 0 1px 4px rgba(0, 0, 0, 0.12); }
.tool { padding: 3px 12px; border: 1px solid var(--green-dark); border-radius: 18px; background: none; color: var(--green-dark); font: inherit; font-size: 0.82rem; font-weight: 600; cursor: pointer; }
.tool:hover:not(:disabled) { background: var(--green-dark); color: #f3ead2; }
.tool:disabled { opacity: 0.4; cursor: default; }
.tool.round { padding: 3px 9px; }
.tool.danger { border-color: var(--danger, #c0392b); color: var(--danger, #c0392b); }
.tool.danger:hover { background: var(--danger, #c0392b); color: #fff; }
.dot { width: 18px; height: 18px; padding: 0; border: 1px solid rgba(0, 0, 0, 0.3); border-radius: 50%; cursor: pointer; }
.dot.green, .item.note.green .item-head { background: #cfe5c4; }
.dot.blue, .item.note.blue .item-head { background: #c6dcee; }
.dot.pink, .item.note.pink .item-head { background: #f1cdd8; }
.dot.yellow, .item.note.yellow .item-head { background: #f3e08f; }
.label-input { font: inherit; font-size: 0.82rem; padding: 2px 8px; border: 1px solid var(--line); border-radius: 12px; background: #fff; width: 150px; }
.status, .session-chip { font-size: 0.78rem; color: var(--muted); background: rgba(251, 246, 230, 0.92); border-radius: 12px; padding: 2px 10px; }
.session-chip { color: var(--green-dark); font-weight: 600; border: 1px solid var(--line); }
.session-chip.none, .session-chip.closed { color: var(--brown); }

.item { position: absolute; display: flex; flex-direction: column; border: 1px solid rgba(60, 45, 20, 0.45); border-radius: 8px; background: #fbf6e6; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.18); }
.item.selected { outline: 2px solid var(--red); outline-offset: 1px; }
.item-head { display: flex; align-items: center; gap: 6px; padding: 3px 4px 3px 9px; border-radius: 7px 7px 0 0; background: #e3d6b3; cursor: move; touch-action: none; min-height: 24px; }
.item-head.fixed { cursor: default; }
.title { flex: 1; min-width: 0; font-size: 0.78rem; font-weight: 700; color: var(--green-dark); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.close, .send { border: 0; background: none; font-size: 1.1rem; line-height: 1; color: var(--muted); cursor: pointer; padding: 0 5px; }
.close:hover { color: var(--danger, #c0392b); }
.send { font-size: 0.95rem; font-weight: 700; color: var(--green-dark); }
.send:hover:not(:disabled) { background: var(--green-dark); color: #f3ead2; border-radius: 8px; }
.send:disabled { opacity: 0.3; cursor: default; }
.item-body { flex: 1; min-height: 0; overflow: auto; padding: 8px 10px; cursor: auto; user-select: text; touch-action: pan-y; }
.item.image .item-body { padding: 0; overflow: hidden; background: #fff; }
.item.note { background: #fff9d6; }
.item.note.green { background: #ecf6e4; }
.item.note.blue { background: #e8f2fa; }
.item.note.pink { background: #fbe9ef; }
.item.note.white { background: #fff; }
.item.note .item-body { padding: 0; }
.note-text { width: 100%; height: 100%; box-sizing: border-box; border: 0; background: transparent; resize: none; padding: 8px 10px; font: inherit; font-size: 0.9rem; color: var(--ink); outline: none; }
.link-handle { position: absolute; right: -14px; top: 50%; transform: translateY(-50%); width: 24px; height: 24px; border-radius: 50%; background: var(--green-dark); color: #f3ead2; font-size: 0.8rem; display: none; align-items: center; justify-content: center; cursor: crosshair; touch-action: none; user-select: none; }
.item:hover .link-handle, .item.selected .link-handle { display: flex; }
.resize { position: absolute; right: 0; bottom: 0; width: 16px; height: 16px; cursor: nwse-resize; touch-action: none; background: linear-gradient(135deg, transparent 50%, rgba(60, 45, 20, 0.45) 50%, rgba(60, 45, 20, 0.45) 60%, transparent 60%, transparent 72%, rgba(60, 45, 20, 0.45) 72%, rgba(60, 45, 20, 0.45) 82%, transparent 82%); }
.empty { position: absolute; left: 50%; top: 42%; transform: translate(-50%, -50%); text-align: center; color: var(--muted); max-width: 420px; pointer-events: none; line-height: 1.5; }
.error { color: var(--danger, #c0392b); }
</style>
