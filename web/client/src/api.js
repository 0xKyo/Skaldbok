// Talking to the server. Player token comes from ?t=..., GM token from ?gm=...; both are kept in the browser and stay in the address
// bar, so every tab says who it is: a GM tab and a player tab can be open at the same time, and each one reloads as itself.
const KEY = 'skaldbok.token';
const GM_KEY = 'skaldbok.gmtoken';

export class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

export function captureToken(win = window) {
  const params = new URLSearchParams(win.location.search);
  const fromUrl = params.get('t');
  if (fromUrl) {
    try {
      win.localStorage.setItem(KEY, fromUrl);
    } catch {
      /* private mode: the token then lives only as long as this page */
    }
    return fromUrl;
  }
  try {
    return win.localStorage.getItem(KEY);
  } catch {
    return null;
  }
}

export function forgetToken(win = window) {
  try {
    win.localStorage.removeItem(KEY);
  } catch {
    /* nothing to forget */
  }
}

export function captureGmToken(win = window) {
  const params = new URLSearchParams(win.location.search);
  const fromUrl = params.get('gm');
  if (fromUrl) {
    try {
      win.localStorage.setItem(GM_KEY, fromUrl);
    } catch {
      /* private mode */
    }
    return fromUrl;
  }
  if (params.get('t')) return null;                     // a tab opened with a player's link is that player's, whatever was saved for the GM
  try {
    return win.localStorage.getItem(GM_KEY);
  } catch {
    return null;
  }
}

export function forgetGmToken(win = window) {
  try {
    win.localStorage.removeItem(GM_KEY);
  } catch {
    /* nothing to forget */
  }
}

// A picture is fetched with the token in a header and shown from a local blob address: an <img> could not send the header.
export async function apiBlobUrl(path, token, fetchImpl = fetch, urls = URL) {
  let res;
  try {
    res = await fetchImpl(`/api${path}`, { headers: { Authorization: `Bearer ${token}` } });
  } catch {
    throw new ApiError(0, 'Cannot reach the server.');
  }
  if (!res.ok) throw new ApiError(res.status, `Error ${res.status}`);
  return urls.createObjectURL(await res.blob());
}

export async function apiGet(path, token, fetchImpl = fetch) {
  let res;
  try {
    res = await fetchImpl(`/api${path}`, { headers: { Authorization: `Bearer ${token}` }, cache: 'no-store' });
  } catch {
    throw new ApiError(0, 'Cannot reach the server.');
  }
  let body = null;
  try {
    body = await res.json();
  } catch {
    /* no JSON body */
  }
  if (!res.ok) throw new ApiError(res.status, body?.error ?? `Error ${res.status}`);
  return body;
}

// A write (POST or PATCH) with a JSON body; the answer is the JSON the server sends back.
export async function apiSend(method, path, token, payload, fetchImpl = fetch) {
  let res;
  try {
    res = await fetchImpl(`/api${path}`, {
      method,
      headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
      cache: 'no-store',
    });
  } catch {
    throw new ApiError(0, 'Cannot reach the server.');
  }
  let body = null;
  try {
    body = await res.json();
  } catch {
    /* no JSON body */
  }
  if (!res.ok) throw new ApiError(res.status, body?.error ?? `Error ${res.status}`);
  return body;
}
