// What you have looked at, to go back and forward between it (the ◀ ▶ buttons at the top). Every part of the app that has something to
// remember — the tab, the Reference page, the adventure and its page — reports its own piece (a "slice") whenever it changes; the whole
// of them together is one step. Going back gives every piece its old value again. Kept in memory: a reload starts again.
//
//   record('adv', {adventure, page})   a piece changed (nothing happens if it is as it was, or if a step is being restored); with `true` as a
//                                      third argument it only corrects the step you are on (what the app picks by itself is not a step)
//   slice('adv')                       the piece as it was in the step you are on
//   back() / forward()                 move between steps; `restoring` is true for a moment so that putting things back is not a new step
import { computed, ref } from 'vue';

const MAX_STEPS = 100;
const RESTORE_MS = 500;                   // how long putting a step back may take (an adventure has to be loaded...)

const same = (a, b) => JSON.stringify(a ?? null) === JSON.stringify(b ?? null);
const copy = (v) => (v === undefined ? undefined : JSON.parse(JSON.stringify(v)));

export function createNav() {
  const steps = ref([{}]);
  const index = ref(0);
  const restoring = ref(false);
  const tick = ref(0);                    // changes with every back / forward: who has a piece, looks at it
  let timer = null;

  const current = computed(() => steps.value[index.value]);
  const canBack = computed(() => index.value > 0);
  const canForward = computed(() => index.value < steps.value.length - 1);

  function record(name, value, replace = false) {
    if (restoring.value) return;
    if (current.value[name] !== undefined && same(current.value[name], value)) return;
    const next = { ...current.value, [name]: copy(value) };
    if (replace || current.value[name] === undefined) {          // the first thing a part says is where it starts, not a step
      steps.value = steps.value.map((s, i) => (i === index.value ? next : s));
      return;
    }
    steps.value = [...steps.value.slice(0, index.value + 1), next].slice(-MAX_STEPS);
    index.value = steps.value.length - 1;
  }

  function go(delta) {
    const to = index.value + delta;
    if (to < 0 || to >= steps.value.length) return;
    index.value = to;
    restoring.value = true;
    tick.value += 1;
    clearTimeout(timer);
    timer = setTimeout(() => { restoring.value = false; }, RESTORE_MS);
  }

  return {
    canBack,
    canForward,
    restoring,
    tick,
    record,
    slice: (name) => copy(current.value[name]),
    back: () => go(-1),
    forward: () => go(1),
  };
}
