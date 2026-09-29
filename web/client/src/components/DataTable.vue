<script setup>
// A table of the rules: optional dice, a roll column when it has dice, then the columns.
defineProps({ table: { type: Object, required: true } }); // {title, dice, columns[], rows[{roll, cells[]}]}
</script>

<template>
  <div class="data-table">
    <div class="dt-title">{{ table.title }}<span v-if="table.dice" class="dt-dice">{{ table.dice }}</span></div>
    <div class="dt-scroll">
      <table>
        <thead>
          <tr>
            <th v-if="table.dice" class="dt-roll">{{ table.dice }}</th>
            <th v-for="(c, i) in table.columns" :key="i">{{ c }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(r, ri) in table.rows" :key="ri">
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
.dt-title { font-family: var(--display); font-weight: 800; font-size: 0.8rem; letter-spacing: 0.1em; text-transform: uppercase; color: var(--brown); margin-bottom: 4px; }
.dt-dice { margin-left: 8px; color: var(--muted); font-weight: 600; }
.dt-scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: 0.86rem; }
th { text-align: left; font-weight: 700; color: var(--brown); border-bottom: 2px solid var(--line); padding: 3px 12px 3px 0; white-space: nowrap; }
td { padding: 3px 12px 3px 0; vertical-align: top; border-bottom: 1px solid var(--line); }
.dt-roll { width: 3.5rem; color: var(--brown); font-weight: 700; white-space: nowrap; }
</style>
