// The editable part of a character sheet, as the server keeps it (the "doc" of GET /api/me), and the two things an editor needs
// to do with it: work out what to send, and take in what the server says without losing what the player is in the middle of.
//
// A change is sent as a "set": the fields that differ, whole, except `attributes` and `coins`, which go key by key so that two people
// changing different attributes at the same time do not undo each other.
const MERGED = ['attributes', 'coins'];

export const clone = (value) => JSON.parse(JSON.stringify(value ?? null));
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

/** The set that turns `base` into `draft` ({} when nothing differs). */
export function diffDoc(base, draft) {
  const set = {};
  for (const key of Object.keys(draft)) {
    if (MERGED.includes(key)) {
      const sub = {};
      for (const k of Object.keys(draft[key] ?? {})) if (!same(base[key]?.[k], draft[key][k])) sub[k] = draft[key][k];
      if (Object.keys(sub).length) set[key] = sub;
    } else if (!same(base[key], draft[key])) {
      set[key] = clone(draft[key]);
    }
  }
  return set;
}

/**
 * The draft after taking in what the server now says (`server`). `base` is what the draft was last in step with: wherever the draft
 * still equals it the player has changed nothing since, so the server's value wins; wherever the player has changed something, theirs stays
 * (and will be sent next).
 */
export function adopt(draft, base, server) {
  const next = clone(draft);
  for (const key of Object.keys(server)) {
    if (MERGED.includes(key)) {
      next[key] = { ...(next[key] ?? {}) };
      for (const k of Object.keys(server[key] ?? {})) if (same(base[key]?.[k], draft[key]?.[k])) next[key][k] = server[key][k];
    } else if (same(base[key], draft[key])) {
      next[key] = clone(server[key]);
    }
  }
  return next;
}

/** Which of the sheet's rule breaks concern this key ("attr:STR", "hp", "encumbrance"...). */
export const issueFor = (issues, key) => (issues ?? []).find((i) => i.key === key);
