import { afterEach, describe, expect, it } from 'vitest';
import { lang, picked, setLang, t, useDefault } from './i18n.js';
import es from './locales/es.js';

const fakeWindow = () => {
  const store = new Map();
  return { localStorage: { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, v) }, document: { documentElement: {} }, store };
};

afterEach(() => {
  lang.value = 'en';
  picked.value = false;
});

describe('t', () => {
  it('gives the English back in English, with its marks filled', () => {
    expect(t('Chat ({n})', { n: 3 })).toBe('Chat (3)');
  });

  it('translates into the language picked, and keeps what the dictionary lacks in English', () => {
    lang.value = 'es';
    expect(t('Chat ({n})', { n: 3 })).toBe(es['Chat ({n})'].replace('{n}', '3'));
    expect(t('Something nobody translated')).toBe('Something nobody translated');
  });

  it('has a Spanish text for every key, with the same marks', () => {
    for (const [key, value] of Object.entries(es)) {
      expect(typeof value, key).toBe('string');
      const marks = (s) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();
      expect(marks(value), key).toEqual(marks(key));
    }
  });
});

describe('the language', () => {
  it('is remembered in this browser once picked', () => {
    const win = fakeWindow();
    setLang('es', win);
    expect(lang.value).toBe('es');
    expect(win.store.get('skaldbok.lang')).toBe('es');
    expect(win.document.documentElement.lang).toBe('es');
  });

  it('follows the GM until somebody picks one here', () => {
    useDefault('es');
    expect(lang.value).toBe('es');
    setLang('en', fakeWindow());
    useDefault('es');
    expect(lang.value).toBe('en');
  });

  it('ignores a language it does not have', () => {
    useDefault('xx');
    expect(lang.value).toBe('en');
    setLang('xx', fakeWindow());
    expect(lang.value).toBe('en');
  });
});
