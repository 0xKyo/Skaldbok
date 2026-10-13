// The language of the page. The texts of the page are written in English and looked up in the dictionary of the language picked
// (locales/<lang>.js: "English text": "translation"); a text it lacks stays in English. The choice is kept in this browser; until
// somebody picks one, the page speaks the GM's language (the server says which). The server is told with every request, so the
// rules, cards and messages it sends come in the same language.
import { ref } from 'vue';
import es from './locales/es.js';

const KEY = 'skaldbok.lang';
const DICTIONARIES = { es };
export const LANGUAGES = [
  { id: 'en', name: 'English' },
  { id: 'es', name: 'Español' },
];

const known = (l) => (LANGUAGES.some((x) => x.id === l) ? l : null);

function stored(win = globalThis.window) {
  try {
    return known(win?.localStorage?.getItem(KEY));
  } catch {
    return null;
  }
}

/** The language the page speaks now ("en", "es"). */
export const lang = ref(stored() ?? 'en');
/** Whether somebody picked it in this browser (otherwise the GM's choice applies). */
export const picked = ref(stored() !== null);

/** Picks the language, and remembers it in this browser. */
export function setLang(l, win = globalThis.window) {
  const id = known(l) ?? 'en';
  lang.value = id;
  picked.value = true;
  try {
    win?.localStorage?.setItem(KEY, id);
  } catch {
    /* private mode: it lasts as long as the page */
  }
  if (win?.document?.documentElement) win.document.documentElement.lang = id;
}

/** The GM's language, used while nobody picked one here. */
export function useDefault(l) {
  if (picked.value || !known(l)) return;
  lang.value = l;
  if (globalThis.document?.documentElement) globalThis.document.documentElement.lang = l;
}

/**
 * The text in the language of the page. `params` fill the "{name}" marks of the text: t('Chat ({n})', { n: 3 }).
 */
export function t(text, params) {
  const dict = DICTIONARIES[lang.value];
  let out = (dict && dict[text]) || text;
  if (params) for (const [k, v] of Object.entries(params)) out = out.split(`{${k}}`).join(String(v));
  return out;
}

/** The locale for dates and numbers. */
export const locale = () => (lang.value === 'es' ? 'es-AR' : 'en-US');
