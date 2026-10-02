// The campaigns of the Master tab: a name and the characters that play it. Each campaign has its own sessions (see sessions.js). A character can be in
// more than one campaign.
import { ref } from 'vue';
import { ApiError, apiGet, apiSend } from './api.js';

export function useCampaigns(token, say = () => {}) {
  const tokenNow = () => (typeof token === 'function' ? token() : token);
  const list = ref([]);                 // [{id, name, created, characters: [ids], sessions}], newest first
  const loaded = ref(false);
  const error = ref('');

  const find = (id) => list.value.find((c) => c.id === id) ?? null;

  async function refresh() {
    try {
      list.value = (await apiGet('/gm/campaigns', tokenNow())).campaigns ?? [];
      error.value = '';
      loaded.value = true;
    } catch (e) {
      error.value = e instanceof ApiError ? e.message : 'Could not open the campaigns.';
    }
  }

  async function create(name = '') {
    try {
      const made = await apiSend('POST', '/gm/campaigns', tokenNow(), name ? { name } : {});
      await refresh();
      say(`Started the campaign “${made.name}”.`);
      return made.id;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not start the campaign.');
      return null;
    }
  }

  async function save(id, patch) {
    try {
      list.value = (await apiSend('PUT', `/gm/campaigns/${encodeURIComponent(id)}`, tokenNow(), patch)).campaigns ?? list.value;
      return true;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not save the campaign.');
      return false;
    }
  }

  const rename = (id, name) => (name.trim() && name.trim() !== find(id)?.name ? save(id, { name: name.trim() }) : Promise.resolve(false));
  const setCharacters = (id, characters) => save(id, { characters });

  // Deletes a campaign with its sessions; gives back what the server answers (the lists after it) so that the sessions can be updated too
  async function remove(id) {
    const name = find(id)?.name ?? 'the campaign';
    try {
      const after = await apiSend('DELETE', `/gm/campaigns/${encodeURIComponent(id)}`, tokenNow());
      list.value = after.campaigns ?? [];
      say(`Deleted “${name}”.`);
      return after;
    } catch (e) {
      say(e instanceof ApiError ? e.message : 'Could not delete the campaign.');
      return null;
    }
  }

  return { list, loaded, error, find, refresh, create, rename, setCharacters, remove };
}
