<script setup>
// "Backup" in the GM header: Export saves all the GM's work in one file; Import (on another computer) puts it back, replacing what is there.
import { ref } from 'vue';
import { ApiError, apiBlobUrl, apiSend } from '../api.js';

const props = defineProps({
  token: { type: String, required: true },
  reload: { type: Function, default: () => window.location.reload() },
});

const open = ref(false);
const message = ref('');
const failed = ref(false);
const pending = ref(null);              // the file chosen, until the GM says yes: { text, files, created, name }
const busy = ref(false);
const fileInput = ref(null);

const say = (text, bad = false) => {
  message.value = text;
  failed.value = bad;
};

async function exportBackup() {
  busy.value = true;
  say('');
  try {
    const url = await apiBlobUrl('/gm/backup', props.token);
    const link = document.createElement('a');
    link.href = url;
    link.download = `skaldbok-backup-${new Date().toISOString().slice(0, 10)}.json`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
    say('Backup saved to your downloads.');
  } catch (e) {
    say(e instanceof ApiError ? e.message : 'The backup could not be made.', true);
  } finally {
    busy.value = false;
  }
}

const readText = (file) =>
  file.text
    ? file.text()
    : new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => resolve(String(reader.result));
        reader.onerror = reject;
        reader.readAsText(file);
      });

async function chosen(event) {
  const file = event.target.files?.[0];
  event.target.value = '';                                  // the same file can be chosen again
  pending.value = null;
  if (!file) return;
  try {
    const text = await readText(file);
    const doc = JSON.parse(text);
    if (doc?.format !== 'skaldbok-backup' || typeof doc.files !== 'object' || doc.files === null) throw new Error('not a backup');
    pending.value = { text, files: Object.keys(doc.files).length, created: String(doc.created ?? '').slice(0, 10), name: file.name };
    say('');
  } catch {
    say('That is not a Skaldbok backup.', true);
  }
}

async function importBackup() {
  const { text } = pending.value;
  busy.value = true;
  try {
    await apiSend('POST', '/gm/backup/import', props.token, JSON.parse(text));
    pending.value = null;
    say('Backup restored.');
    props.reload();
  } catch (e) {
    say(e instanceof ApiError ? e.message : 'The backup could not be restored.', true);
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <span class="backup">
    <button type="button" class="backup-btn" data-test="backup-toggle" :aria-expanded="open" title="Move your work to another computer" @click="open = !open">Backup</button>
    <div v-if="open" class="backup-panel" data-test="backup-panel">
      <button type="button" class="btn" data-test="backup-export" :disabled="busy" @click="exportBackup">Export backup</button>
      <button type="button" class="btn" data-test="backup-import" :disabled="busy" @click="fileInput.click()">Import backup…</button>
      <input ref="fileInput" type="file" accept=".json,application/json" hidden data-test="backup-file" @change="chosen" />
      <p class="muted small">Export saves characters, chat, campaigns, sessions, boards, notes and homebrew in one file. Import it on the other computer.</p>
      <div v-if="pending" class="backup-confirm" data-test="backup-confirm">
        <p>
          Replace everything here with “{{ pending.name }}”<span v-if="pending.created"> ({{ pending.created }})</span>, {{ pending.files }} files? What is here now is deleted.
        </p>
        <button type="button" class="btn danger" data-test="backup-yes" :disabled="busy" @click="importBackup">Yes, replace</button>
        <button type="button" class="btn" data-test="backup-no" @click="pending = null">Cancel</button>
      </div>
      <p v-if="message" :class="failed ? 'error' : 'ok'" data-test="backup-message">{{ message }}</p>
    </div>
  </span>
</template>

<style scoped>
.backup { position: relative; flex: none; }
.backup-btn { background: none; border: 1px solid var(--green-dark); border-radius: 14px; color: var(--green-dark); font: inherit; font-size: 0.75rem; padding: 3px 10px; cursor: pointer; }
.backup-btn:hover { background: var(--green-dark); color: #f3ead2; }
.backup-panel { position: absolute; right: 0; top: calc(100% + 6px); z-index: 20; width: 300px; display: flex; flex-direction: column; gap: 8px; padding: 12px; background: var(--paper); border: 1px solid var(--green-dark); border-radius: 8px; box-shadow: 0 4px 14px rgba(0, 0, 0, 0.2); }
.backup-panel p { margin: 0; }
.ok { color: var(--green-dark); }
</style>
