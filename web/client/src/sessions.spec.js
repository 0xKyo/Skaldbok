import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { nextTick } from 'vue';
import { useSessions } from './sessions.js';
import { fakeServer } from './test/fakeSessions.js';

let server;
let said;

beforeEach(() => {
  vi.useFakeTimers();
  server = fakeServer();
  server.install();
  said = [];
});
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); });

let CAMPAIGN;
const open = async () => {
  const sessions = useSessions('gm', (t) => said.push(t));
  server.campaigns.push({ id: 'c1', name: 'The Misty Vale', created: '2026-10-01T00:00:00Z', characters: [] });
  CAMPAIGN = 'c1';
  await sessions.refresh();
  return sessions;
};

describe('useSessions', () => {
  it('starts with no sessions', async () => {
    const sessions = await open();
    expect(sessions.list.value).toEqual([]);
    expect(sessions.active.value).toBe('');
    expect(sessions.loaded.value).toBe(true);
  });

  it('a new session is the active one, and starting another closes it', async () => {
    const sessions = await open();
    const first = await sessions.create(CAMPAIGN);
    expect(sessions.active.value).toBe(first);
    expect(sessions.activeSession().name).toBe('Session 1');
    const second = await sessions.create(CAMPAIGN, 'The ogre');
    expect(sessions.active.value).toBe(second);
    expect(sessions.find(first).ended).toBeTruthy();
    expect(sessions.list.value.map((s) => s.name)).toEqual(['The ogre', 'Session 1']);   // the newest first
  });

  it('ending a session leaves none active, and opening a closed one makes it the active one', async () => {
    const sessions = await open();
    const a = await sessions.create(CAMPAIGN);
    const b = await sessions.create(CAMPAIGN);
    await sessions.end(b);
    expect(sessions.active.value).toBe('');
    await sessions.open(a);
    expect(sessions.active.value).toBe(a);
    expect(sessions.find(a).ended).toBe('');
    await sessions.open(b);                               // the other one is closed
    expect(sessions.active.value).toBe(b);
    expect(sessions.find(a).ended).toBeTruthy();
  });

  it('the board of a closed session cannot be changed; opening it again lets it', async () => {
    const sessions = await open();
    const a = await sessions.create(CAMPAIGN);
    const board = sessions.boardFor(a);
    await board.ready;
    expect(board.readOnly.value).toBe(false);
    await sessions.end(a);
    expect(board.readOnly.value).toBe(true);
    expect(board.addNote()).toBeNull();
    await sessions.open(a);
    expect(board.readOnly.value).toBe(false);
    expect(board.addNote()).toBeTruthy();
  });

  it('what is written in a session is saved to it, and survives asking for its board again', async () => {
    const sessions = await open();
    const a = await sessions.create(CAMPAIGN);
    const board = sessions.boardFor(a);
    await board.ready;
    board.addNote('green', 'They beat the ogre');
    await nextTick();
    await vi.advanceTimersByTimeAsync(900);
    expect(server.sessions[0].board.items[0].text).toBe('They beat the ogre');
    expect(sessions.boardFor(a)).toBe(board);              // the same board: it is kept, not made again
  });

  it('sends a copy of an item of the Screen to the active session, and says so', async () => {
    const sessions = await open();
    const a = await sessions.create(CAMPAIGN, 'Night one');
    const item = { id: 'n1', kind: 'note', x: 5, y: 6, w: 220, h: 160, z: 1, color: 'yellow', text: 'The loot: a silver ring' };
    expect(await sessions.send(item)).toBe(true);
    const stored = server.sessions[0].board.items;
    expect(stored).toHaveLength(1);
    expect(stored[0]).toMatchObject({ kind: 'note', text: 'The loot: a silver ring' });
    expect(stored[0].id).not.toBe('n1');                   // a copy of its own
    expect(said.at(-1)).toContain('Sent to “Night one”');
    expect(sessions.find(a).items).toBe(1);
  });

  it('there is nowhere to send it when no session is active', async () => {
    const sessions = await open();
    expect(await sessions.send({ id: 'n1', kind: 'note', x: 0, y: 0, w: 10, h: 10, text: 'x' })).toBe(false);
    expect(said.at(-1)).toContain('no active session');
    const a = await sessions.create(CAMPAIGN);
    await sessions.end(a);
    expect(await sessions.send({ id: 'n1', kind: 'note', x: 0, y: 0, w: 10, h: 10, text: 'x' })).toBe(false);
    expect(server.sessions[0].board.items).toHaveLength(0);
  });

  it('renames a session', async () => {
    const sessions = await open();
    const a = await sessions.create(CAMPAIGN);
    expect(await sessions.rename(a, '  The Isle of Mist ')).toBe(true);
    expect(server.sessions[0].name).toBe('The Isle of Mist');
    expect(sessions.find(a).name).toBe('The Isle of Mist');
    expect(await sessions.rename(a, '   ')).toBe(false);
  });
});
