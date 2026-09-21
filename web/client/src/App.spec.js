import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import App from './App.vue';
import me from './test/fixtures/me.json';
import party from './test/fixtures/party.json';
import content from './test/fixtures/content.json';
import spells from './test/fixtures/spells.json';
import chat from './test/fixtures/chat.json';

// A fake server: routes by path, checks the Bearer token.
function server(routes, goodToken = 'good-token') {
  return vi.fn(async (url, opts = {}) => {
    const token = (opts.headers?.Authorization ?? '').replace('Bearer ', '');
    if (token !== goodToken) return { ok: false, status: 401, json: async () => ({ error: 'This link is not valid.' }) };
    if (opts.method && opts.method !== 'GET') return { ok: true, status: 200, json: async () => ({ ok: true }) };
    const path = url.replace('/api', '');
    const body = routes[path.split('?')[0]];
    return body ? { ok: true, status: 200, json: async () => structuredClone(body) } : { ok: false, status: 404, json: async () => ({ error: 'Not found' }) };
  });
}

beforeEach(() => {
  localStorage.clear();
  window.history.replaceState(null, '', '/');
  vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
});
afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

describe('App', () => {
  it('without a personal link it explains what to do and asks the server nothing', async () => {
    const f = server({});
    vi.stubGlobal('fetch', f);
    const w = mount(App);
    await flushPromises();
    expect(w.find('[data-test=no-link]').exists()).toBe(true);
    expect(w.text()).toContain('personal link');
    expect(f).not.toHaveBeenCalled();
  });

  it('with the link it shows the sheet, and removes the token from the address bar', async () => {
    vi.stubGlobal('fetch', server({ '/me': me, '/party': party }));
    window.history.replaceState(null, '', '/?t=good-token');
    const w = mount(App);
    await flushPromises();
    expect(w.get('[data-test=name]').text()).toContain('Brenna');
    expect(w.get('[data-test=who]').text()).toContain('Human Fighter');
    expect(window.location.search).toBe('');
    expect(localStorage.getItem('skaldbok.token')).toBe('good-token');
    expect(document.title).toBe('Brenna · Skaldbok');
  });

  it('a link that does not work is forgotten and explained', async () => {
    vi.stubGlobal('fetch', server({ '/me': me, '/party': party }));
    localStorage.setItem('skaldbok.token', 'stale-token');
    const w = mount(App);
    await flushPromises();
    expect(w.find('[data-test=no-link]').exists()).toBe(true);
    expect(w.text()).toContain('not valid');
    expect(localStorage.getItem('skaldbok.token')).toBeNull();
  });

  it('switches between the sheet, the party and the rules', async () => {
    vi.stubGlobal('fetch', server({ '/me': me, '/party': party, '/content': content, '/content/spells': spells }));
    localStorage.setItem('skaldbok.token', 'good-token');
    const w = mount(App);
    await flushPromises();
    const tabs = w.findAll('[role=tab]');
    expect(tabs.map((t) => t.text())).toEqual(['My character', 'Party', 'Chat', 'Rules']);
    await tabs[1].trigger('click');
    expect(w.get('[data-test=party-name]').text()).toBe('The Misty Vale party');
    expect(w.findAll('[data-test=member]').length).toBe(party.party.members.length);
    expect(w.text()).toContain('you');
    await tabs[3].trigger('click');
    await flushPromises();
    expect(w.text()).toContain('Rime Ward');
    expect(w.text()).toContain('Homebrew · Frostmarch');
  });

  it('counts what the GM wrote since the player last read, and tells the server when they open the chat', async () => {
    const thread = structuredClone(chat);
    const f = server({ '/me': me, '/party': party, '/chat': thread });
    vi.stubGlobal('fetch', f);
    localStorage.setItem('skaldbok.token', 'good-token');
    const w = mount(App);
    await flushPromises();
    const tab = () => w.findAll('[role=tab]')[2];
    expect(tab().text()).toBe('Chat (1)');
    await tab().trigger('click');
    await flushPromises();
    expect(tab().text()).toBe('Chat');
    expect(w.findAll('[data-test=message]')).toHaveLength(2);
    expect(w.findAll('[data-test=broadcast]')).toHaveLength(1);
    const read = f.mock.calls.find(([url]) => url === '/api/chat/read');
    expect(JSON.parse(read[1].body)).toEqual({ upTo: 'm-000000000003-cccccc' });
    await w.findAll('[role=tab]')[0].trigger('click');
    thread.messages.push({ id: 'm-000000000004-dddddd', at: '2026-09-20T19:00:00Z', from: 'gm', kind: 'message', to: '', text: 'One more.', image: null });
    thread.playerRead = 'm-000000000003-cccccc';
    await vi.advanceTimersByTimeAsync(3100);
    await flushPromises();
    expect(tab().text()).toBe('Chat (1)');
  });

  it('works with a server that has no chat yet', async () => {
    vi.stubGlobal('fetch', server({ '/me': me, '/party': party }));
    localStorage.setItem('skaldbok.token', 'good-token');
    const w = mount(App);
    await flushPromises();
    expect(w.get('[data-test=name]').text()).toContain('Brenna');
    expect(w.text()).not.toContain('offline');
    expect(w.findAll('[role=tab]')[2].text()).toBe('Chat');
  });

  it('keeps showing the last data when the connection drops, and says so', async () => {
    const f = server({ '/me': me, '/party': party });
    vi.stubGlobal('fetch', f);
    localStorage.setItem('skaldbok.token', 'good-token');
    const w = mount(App);
    await flushPromises();
    f.mockImplementation(async () => { throw new TypeError('offline'); });
    await vi.advanceTimersByTimeAsync(3100);
    await flushPromises();
    expect(w.get('[data-test=name]').text()).toContain('Brenna');
    expect(w.text()).toContain('offline');
  });

  it('refreshes by itself: a change the GM makes shows up', async () => {
    const changing = structuredClone(me);
    const f = server({ '/me': changing, '/party': party });
    vi.stubGlobal('fetch', f);
    localStorage.setItem('skaldbok.token', 'good-token');
    const w = mount(App);
    await flushPromises();
    expect(w.findAll('[data-test=meter-value]')[0].text()).toBe('10 / 13');
    changing.hp.current = 4;
    await vi.advanceTimersByTimeAsync(3100);
    await flushPromises();
    expect(w.findAll('[data-test=meter-value]')[0].text()).toBe('4 / 13');
  });
});
