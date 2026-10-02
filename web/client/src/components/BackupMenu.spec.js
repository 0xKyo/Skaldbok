import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import BackupMenu from './BackupMenu.vue';

let calls;
let reload;

beforeEach(() => {
  calls = [];
  reload = vi.fn();
  URL.createObjectURL = vi.fn(() => 'blob:x');
  URL.revokeObjectURL = vi.fn();
  vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
    calls.push({ url, method: opts.method ?? 'GET', body: opts.body });
    if (url === '/api/gm/backup/import') return opts.body.includes('bad') ? { ok: false, status: 400, json: async () => ({ error: 'The backup is damaged.' }) } : { ok: true, status: 200, json: async () => ({ ok: true, files: 2 }) };
    return { ok: true, status: 200, blob: async () => new Blob(['{}']), json: async () => ({}) };
  }));
});
afterEach(() => vi.unstubAllGlobals());

const mountMenu = () => mount(BackupMenu, { props: { token: 'gm', reload }, attachTo: document.body });
const open = async (w) => { await w.get('[data-test=backup-toggle]').trigger('click'); return w; };
const pick = async (w, text, name = 'skaldbok-backup.json') => {
  const input = w.get('[data-test=backup-file]');
  const file = new File([text], name, { type: 'application/json' });
  Object.defineProperty(input.element, 'files', { value: [file], configurable: true });
  await input.trigger('change');
  await vi.waitFor(() => expect(w.find('[data-test=backup-confirm], [data-test=backup-message]').exists()).toBe(true));
};

describe('BackupMenu', () => {
  it('stays closed until asked', async () => {
    const w = mountMenu();
    expect(w.find('[data-test=backup-panel]').exists()).toBe(false);
    await open(w);
    expect(w.find('[data-test=backup-panel]').exists()).toBe(true);
  });

  it('Export downloads the backup of the server', async () => {
    const w = await open(mountMenu());
    await w.get('[data-test=backup-export]').trigger('click');
    await flushPromises();
    expect(calls[0].url).toBe('/api/gm/backup');
    expect(URL.createObjectURL).toHaveBeenCalled();
    expect(w.get('[data-test=backup-message]').text()).toContain('downloads');
  });

  it('Import asks before it replaces anything, and reloads when it is done', async () => {
    const w = await open(mountMenu());
    await pick(w, JSON.stringify({ format: 'skaldbok-backup', version: 1, created: '2026-10-02T03:00:00Z', files: { 'board.json': 'e30=', 'campaigns/c.json': 'e30=' } }));
    expect(w.get('[data-test=backup-confirm]').text()).toContain('2 files');
    expect(calls).toHaveLength(0);                                           // nothing was sent yet
    await w.get('[data-test=backup-yes]').trigger('click');
    await flushPromises();
    expect(calls[0]).toMatchObject({ url: '/api/gm/backup/import', method: 'POST' });
    expect(reload).toHaveBeenCalled();
  });

  it('Cancel sends nothing', async () => {
    const w = await open(mountMenu());
    await pick(w, JSON.stringify({ format: 'skaldbok-backup', files: {} }));
    await w.get('[data-test=backup-no]').trigger('click');
    expect(w.find('[data-test=backup-confirm]').exists()).toBe(false);
    expect(calls).toHaveLength(0);
  });

  it('a file that is not a backup is refused on the spot', async () => {
    const w = await open(mountMenu());
    await pick(w, '{"hello": 1}');
    expect(w.get('[data-test=backup-message]').text()).toContain('not a Skaldbok backup');
    await pick(w, 'not json at all');
    expect(w.get('[data-test=backup-message]').text()).toContain('not a Skaldbok backup');
    expect(w.find('[data-test=backup-confirm]').exists()).toBe(false);
  });

  it('shows what the server says when it refuses, and does not reload', async () => {
    const w = await open(mountMenu());
    await pick(w, JSON.stringify({ format: 'skaldbok-backup', files: { bad: 'x' } }));
    await w.get('[data-test=backup-yes]').trigger('click');
    await flushPromises();
    expect(w.get('[data-test=backup-message]').text()).toContain('damaged');
    expect(reload).not.toHaveBeenCalled();
  });
});
