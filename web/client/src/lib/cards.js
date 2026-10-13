// The cards of the Reference (spells, abilities...), fetched once for a whole page and kept: a sheet that shows a card without leaving
// itself asks for it here when it is opened.
import { apiGet } from '../api.js';

const lists = new Map();           // "<prefix>|<token>|<type>" -> the promise of the list of cards of that type

/** The card with that key ({key, name, subtitle, fields[], body, ...}), or null when it is not there (or the Reference cannot be read). */
export async function cardOf(prefix, token, type, key) {
  const id = `${prefix}|${token}|${type}`;
  if (!lists.has(id)) {
    lists.set(id, apiGet(`${prefix}/content/${type}`, token).then((r) => r.entries ?? []).catch(() => { lists.delete(id); return []; }));
  }
  return (await lists.get(id)).find((c) => c.key === key) ?? null;
}

/** Forgets what was fetched (a new language, or content that has changed). */
export const forgetCards = () => lists.clear();
