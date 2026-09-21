<script setup>
import Meter from './Meter.vue';

defineProps({ party: { type: Object, default: null } });
</script>

<template>
  <section aria-label="Party">
    <div v-if="!party" class="panel">
      <h2>Party</h2>
      <p class="muted">You are not in a party yet. Your GM can add you to one.</p>
    </div>
    <template v-else>
      <h2 data-test="party-name">{{ party.name }}</h2>
      <div class="grid two">
        <article v-for="m in party.members" :key="m.name + m.player" class="panel member" data-test="member">
          <h3>
            {{ m.name }}<span v-if="m.nickname" class="gold"> "{{ m.nickname }}"</span>
            <span v-if="m.you" class="chip you" style="margin-left: 6px">you</span>
          </h3>
          <div class="muted small">{{ m.kin }} {{ m.profession }} · {{ m.age }}<span v-if="m.player"> · played by {{ m.player }}</span></div>
          <Meter label="HP" :current="m.hp.current" :max="m.hp.max" kind="hp" />
          <Meter label="WP" :current="m.wp.current" :max="m.wp.max" kind="wp" />
          <div class="chips">
            <span v-for="c in m.conditions" :key="c.name" class="chip on">{{ c.name }}</span>
            <span v-if="!m.conditions.length" class="muted small">No conditions</span>
          </div>
        </article>
      </div>
    </template>
  </section>
</template>
