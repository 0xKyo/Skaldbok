import { describe, expect, it, vi } from 'vitest';
import { ApiError, apiGet, apiSend, captureToken, forgetToken } from './api.js';

// a tiny stand-in for window: a location, a history and a storage
function fakeWindow(url) {
  const u = new URL(url);
  const store = new Map();
  const win = {
    location: { search: u.search, pathname: u.pathname, hash: u.hash },
    history: { replaceState: vi.fn((s, t, next) => { const n = new URL(next, 'http://x'); win.location.search = n.search; win.location.hash = n.hash; }) },
    localStorage: { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, v), removeItem: (k) => store.delete(k) },
  };
  return win;
}

describe('captureToken', () => {
  it('takes the token from the personal link, keeps it, and removes it from the address bar', () => {
    const win = fakeWindow('http://x/?t=abc123&lang=en#party');
    expect(captureToken(win)).toBe('abc123');
    expect(win.history.replaceState).toHaveBeenCalledWith(null, '', '/?lang=en#party');
    expect(win.localStorage.getItem('skaldbok.token')).toBe('abc123');
  });

  it('remembers the token for later visits without the link', () => {
    const win = fakeWindow('http://x/?t=abc123');
    captureToken(win);
    win.location.search = '';
    expect(captureToken(win)).toBe('abc123');
  });

  it('a new link replaces the old token', () => {
    const win = fakeWindow('http://x/?t=old');
    captureToken(win);
    win.location.search = '?t=new';
    expect(captureToken(win)).toBe('new');
  });

  it('has nothing to offer without a link or a saved token, and forgets on request', () => {
    const win = fakeWindow('http://x/');
    expect(captureToken(win)).toBeNull();
    win.localStorage.setItem('skaldbok.token', 'zzz');
    forgetToken(win);
    expect(captureToken(win)).toBeNull();
  });
});

describe('apiSend', () => {
  it('sends a JSON body with the token in a header', async () => {
    const f = vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ done: 1 }) }));
    expect(await apiSend('PATCH', '/me', 'tok', { set: { hp: 7 } }, f)).toEqual({ done: 1 });
    expect(f).toHaveBeenCalledWith('/api/me', expect.objectContaining({ method: 'PATCH', headers: expect.objectContaining({ Authorization: 'Bearer tok', 'Content-Type': 'application/json' }), body: '{"set":{"hp":7}}' }));
    expect(f.mock.calls[0][0]).not.toContain('tok');
  });

  it('turns a refusal into an ApiError with the server\'s message and status', async () => {
    const error = await apiSend('PATCH', '/me', 'tok', {}, vi.fn(async () => ({ ok: false, status: 423, json: async () => ({ error: 'Your GM has locked your sheet.' }) }))).catch((e) => e);
    expect(error).toBeInstanceOf(ApiError);
    expect([error.status, error.message]).toEqual([423, 'Your GM has locked your sheet.']);
  });

  it('reports a dead connection as status 0', async () => {
    const error = await apiSend('POST', '/chat', 'tok', {}, vi.fn(async () => { throw new TypeError('failed'); })).catch((e) => e);
    expect(error.status).toBe(0);
  });
});

describe('apiGet', () => {
  const answer = (status, body) => vi.fn(async () => ({ ok: status < 400, status, json: async () => body }));

  it('sends the token as a Bearer header, never in the URL', async () => {
    const f = answer(200, { hello: 1 });
    expect(await apiGet('/me', 'tok', f)).toEqual({ hello: 1 });
    expect(f).toHaveBeenCalledWith('/api/me', expect.objectContaining({ headers: { Authorization: 'Bearer tok' } }));
    expect(f.mock.calls[0][0]).not.toContain('tok');
  });

  it('turns an error answer into an ApiError with the server message and status', async () => {
    const error = await apiGet('/me', 'tok', answer(401, { error: 'This link is not valid.' })).catch((e) => e);
    expect(error).toBeInstanceOf(ApiError);
    expect([error.status, error.message]).toEqual([401, 'This link is not valid.']);
  });

  it('reports a dead connection as status 0', async () => {
    const error = await apiGet('/me', 'tok', vi.fn(async () => { throw new TypeError('failed'); })).catch((e) => e);
    expect(error.status).toBe(0);
  });
});
