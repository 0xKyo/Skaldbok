<script setup>
// A table of the rules: optional dice, a roll column when it has dice, then the columns. A table with a die can be rolled:
// the button picks a random result and marks its row.
import { computed, ref } from 'vue';
import PinButton from './PinButton.vue';

const props = defineProps({
  table: { type: Object, required: true },        // {title, dice, columns[], rows[{roll, cells[]}]}
  pinnable: { type: Boolean, default: true },     // the 📌 button (the GM's board has none: it is already there)
  hideTitle: { type: Boolean, default: false },   // when what holds the table already says its name
});

const sides = computed(() => Number.parseInt((props.table.dice ?? '').replace(/\D/g, ''), 10) || 0);
const rolled = ref(null); // {value, row}

// the row a roll lands on: "3" or a range like "4-5"; without roll texts, the row at that position
function rowFor(value) {
  const at = props.table.rows.findIndex((r) => {
    const m = (r.roll ?? '').match(/^\s*(\d+)(?:\s*[-–]\s*(\d+))?\s*$/);
    return m && value >= Number(m[1]) && value <= Number(m[2] ?? m[1]);
  });
  return at >= 0 ? at : Math.min(value, props.table.rows.length) - 1;
}

// what the 📌 button puts on the master's board: this table
const pinTable = () => ({ type: 'table', key: `table:${props.table.title}`, title: props.table.title, data: { title: props.table.title, dice: props.table.dice, columns: props.table.columns, rows: props.table.rows }, w: 360, h: 280 });

function roll() {
  const value = 1 + Math.floor(Math.random() * sides.value);
  rolled.value = { value, row: rowFor(value) };
}
</script>

<template>
  <div class="data-table">
    <div class="dt-title">
      <template v-if="!hideTitle">{{ table.title }}</template><span v-if="table.dice" class="dt-dice">{{ table.dice }}</span>
      <button v-if="sides" type="button" class="dt-roll-btn" data-test="table-roll" @click="roll">Roll {{ table.dice }}</button>
      <span v-if="rolled" class="dt-result" data-test="table-rolled">rolled {{ rolled.value }}</span>
      <PinButton v-if="pinnable" :snapshot="pinTable" small />
    </div>
    <div class="dt-scroll">
      <table>
        <thead>
          <tr>
            <th v-if="table.dice" class="dt-roll">{{ table.dice }}</th>
            <th v-for="(c, i) in table.columns" :key="i">{{ c }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(r, ri) in table.rows" :key="ri" :class="{ picked: rolled && rolled.row === ri }">
            <td v-if="table.dice" class="dt-roll">{{ r.roll }}</td>
            <td v-for="(cell, ci) in r.cells" :key="ci">{{ cell }}</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.data-table { margin: 0.9em 0 1em; }
.dt-title { font-family: var(--display); font-weight: 800; font-size: 0.8rem; letter-spacing: 0.1em; text-transform: uppercase; color: var(--brown); margin-bottom: 4px; display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.dt-dice { color: var(--muted); font-weight: 600; }
.dt-roll-btn { padding: 1px 12px; border: 1px solid var(--green-dark); border-radius: 20px; background: none; color: var(--green-dark); font: inherit; font-size: 0.75rem; font-weight: 700; letter-spacing: 0.04em; text-transform: none; cursor: pointer; }
.dt-roll-btn:hover { background: var(--green-dark); color: #f3ead2; }
.dt-result { color: var(--green-dark); font-size: 0.8rem; text-transform: none; letter-spacing: 0; }
.dt-scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: 0.86rem; }
th { text-align: left; font-weight: 700; color: var(--brown); border-bottom: 2px solid var(--line); padding: 3px 12px 3px 0; white-space: nowrap; }
td { padding: 3px 12px 3px 0; vertical-align: top; border-bottom: 1px solid var(--line); }
tr.picked td { background: rgba(212, 175, 55, 0.28); }
.dt-roll { width: 3.5rem; color: var(--brown); font-weight: 700; white-space: nowrap; }
</style>
