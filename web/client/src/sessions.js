// The sessions of the Master tab: a board for each game played, inside a campaign (campaigns.js). One session is active at a time, in whichever campaign (what is sent from the Screen goes to it); the others are
// closed, which means they can be looked at but not changed, until one is opened again (Open Session makes it the active one and closes the other).
// Each session's board is a useBoard() of its own, made when it is first looked at and kept, so that sending something to a session that is not on
// screen and looking at it later is the same thing.
import { ref } from 'vue';
import { ApiError, apiGet, apiSend } from './api.js';
import { useBoard } from './board.js';

export function useSessions(token, say = () => {}) {
  const tokenNow = () => (typeof token === 'function' ? token() : token);
  const list = ref([]);                 // [{id, campaign, name, created, ended, items}], newest first
  const active = ref('');               // the id of the session that is open, or ''
  const loaded = ref(false);
  const error = ref('');
  const boards = {};                    // id -> its board

  const find = (id) => list.value.find((s) => s.id === id) ?? null;
  const activeSession = () => find(active.value);
  const ofCampaign = (campaign) => list.value.filter((s) => s.campaign === campaign);

  function take(data) {
    list.value = data.sessions ?? [];
    active.value = data.active ?? '';
    for (const [id, board] of Object.entries(boards)) board.readOnly.value = !!find(id)?.ended;      // a closed session cannot be changed
  }

  async function refresh() {
    try {
      take(await apiGet('/gm/sessions', tokenNow()));
      error.value = '';
      loaded.value = true;
    } catch (e) {
      error.value = e instanceof ApiError ? e.message : 'Could not open the sessions.';
    }
  }

  // The board of a session, made (and read) the first time it is asked for
  function boardFor(id) {
    if (!boards[id]) {
      const board = useBoard(token, {
        read: async () => (await apiGet(`/gm/sessions/${encodeURIComponent(id)}`, tokenNow())).board,
        write: (data) => apiSend('PUT', `/gm/sessions/${encodeURIComponent(id)}`, tokenNow(), { board: data }),
      });
      board.readOnly.value = !!find(id)?.ended;
      board.ready = board.load();
      boards[id] = board;
    }
    return boards[id];
  }

  async function create(campaign, name = '') {
    try {
      const made = await apiSend('POST', '/gm/sessions', tokenNow(), name ? { campaign, name } : { campaign });
      await refresh();
      say(`Started “${made.name}”.`);
      return made.id;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not start the session.');
      return null;
    }
  }

  async function end(id) {
    try {
      await boards[id]?.save();                                  // what was written a moment ago is kept before it is closed
      take(await apiSend('POST', `/gm/sessions/${encodeURIComponent(id)}/end`, tokenNow()));
      say(`Ended “${find(id)?.name ?? 'the session'}”.`);
      return true;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not end the session.');
      return false;
    }
  }

  async function open(id) {
    try {
      await boards[active.value]?.save();
      take(await apiSend('POST', `/gm/sessions/${encodeURIComponent(id)}/open`, tokenNow()));
      say(`“${find(id)?.name ?? 'The session'}” is the active session.`);
      return true;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not open the session.');
      return false;
    }
  }

  // Deletes a session with everything on it (the active one too: then there is none)
  async function remove(id) {
    const name = find(id)?.name ?? 'the session';
    try {
      delete boards[id];
      take(await apiSend('DELETE', `/gm/sessions/${encodeURIComponent(id)}`, tokenNow()));
      say(`Deleted “${name}”.`);
      return true;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not delete the session.');
      return false;
    }
  }

  async function rename(id, name) {
    const clean = name.trim();
    if (!clean || clean === find(id)?.name) return false;
    try {
      await apiSend('PUT', `/gm/sessions/${encodeURIComponent(id)}`, tokenNow(), { name: clean });
      const s = find(id);
      if (s) s.name = clean;
      return true;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not rename the session.');
      return false;
    }
  }

  // Sends a copy of an item of the Screen to the active session
  async function send(item) {
    const target = activeSession();
    if (!target) {
      say('There is no active session: start one in Sessions.');
      return false;
    }
    const board = boardFor(target.id);
    await board.ready;
    const id = board.addCopy(item);
    if (!id) {
      say(board.loadError.value || 'Could not send it to the session.');
      return false;
    }
    await board.save();
    const s = find(target.id);
    if (s) s.items = board.items.value.length;
    say(`Sent to “${target.name}”.`);
    return true;
  }

  return { list, active, loaded, error, refresh, take, boardFor, create, end, open, remove, rename, send, activeSession, ofCampaign, find };
}
