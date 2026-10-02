// A tiny stand-in for the server's campaigns, sessions and board pictures, for the tests: it keeps them in memory and answers like the real routes.
import { vi } from 'vitest';

export function fakeServer({ withScreen = true } = {}) {
  const server = {
    campaigns: [],              // [{id, name, created, characters}]
    sessions: [],               // [{id, campaign, name, created, ended, reopened, board}]
    screen: { items: [], arrows: [], view: { x: 0, y: 0, zoom: 1 } },
    images: [],
    calls: [],
    clock: 0,
  };
  const iso = () => new Date(Date.UTC(2026, 9, 2, 3, 0, server.clock++)).toISOString();
  const empty = () => ({ items: [], arrows: [], view: { x: 0, y: 0, zoom: 1 } });
  const ok = (data, status = 200) => ({ ok: true, status, json: async () => structuredClone(data), blob: async () => new Blob(['x']) });
  const fail = (status, error) => ({ ok: false, status, json: async () => ({ error }) });
  const sessionList = () => ({
    sessions: [...server.sessions].reverse().map((s) => ({ id: s.id, campaign: s.campaign, name: s.name, created: s.created, ended: s.ended, items: s.board.items.length })),
    active: server.sessions.find((s) => !s.ended)?.id ?? '',
  });
  const campaignList = () => ({
    campaigns: [...server.campaigns].reverse().map((c) => ({ ...c, sessions: server.sessions.filter((s) => s.campaign === c.id).length })),
  });

  server.install = () => {
    vi.stubGlobal('fetch', vi.fn(async (url, opts = {}) => {
      const path = url.replace('/api', '').split('?')[0];
      const method = opts.method ?? 'GET';
      const body = opts.body ? JSON.parse(opts.body) : null;
      server.calls.push({ method, path, body });
      if (path === '/gm/board') {
        if (method === 'PUT') { server.screen = body; return ok({ ok: true }); }
        return withScreen ? ok(server.screen) : fail(500, 'broken');
      }
      if (path === '/gm/board/images' && method === 'POST') { server.images.push(body.image); return ok({ id: `i${server.images.length}.png` }, 201); }
      if (path.startsWith('/gm/board/images/')) return ok({});

      if (path === '/gm/campaigns' && method === 'GET') return ok(campaignList());
      if (path === '/gm/campaigns' && method === 'POST') {
        const c = { id: `c${server.campaigns.length + 1}`, name: body?.name || `Campaign ${server.campaigns.length + 1}`, created: iso(), characters: [] };
        server.campaigns.push(c);
        return ok(c, 201);
      }
      const cm = path.match(/^\/gm\/campaigns\/([^/]+)$/);
      if (cm) {
        const c = server.campaigns.find((x) => x.id === cm[1]);
        if (!c) return fail(404, 'There is no such campaign.');
        if (method === 'PUT') {
          if ('name' in body) { if (!body.name.trim()) return fail(400, 'A campaign needs a name.'); c.name = body.name; }
          if ('characters' in body) c.characters = [...new Set(body.characters)];
          return ok(campaignList());
        }
        if (method === 'DELETE') {
          server.sessions = server.sessions.filter((s) => s.campaign !== c.id);
          server.campaigns.splice(server.campaigns.indexOf(c), 1);
          return ok({ campaigns: campaignList().campaigns, sessions: sessionList().sessions, active: sessionList().active });
        }
      }

      if (path === '/gm/sessions' && method === 'GET') return ok(sessionList());
      if (path === '/gm/sessions' && method === 'POST') {
        if (!body?.campaign || !server.campaigns.some((c) => c.id === body.campaign)) return fail(400, 'A session belongs to a campaign: choose one, or start a campaign first.');
        server.sessions.filter((s) => !s.ended).forEach((s) => { s.ended = iso(); });
        const inCampaign = server.sessions.filter((s) => s.campaign === body.campaign).length;
        const s = { id: `s${server.sessions.length + 1}`, campaign: body.campaign, name: body.name || `Session ${inCampaign + 1}`, created: iso(), ended: '', reopened: 0, board: empty() };
        server.sessions.push(s);
        return ok(s, 201);
      }
      const m = path.match(/^\/gm\/sessions\/([^/]+)(?:\/(end|open))?$/);
      if (m) {
        const s = server.sessions.find((x) => x.id === m[1]);
        if (!s) return fail(404, 'There is no such session.');
        if (!m[2] && method === 'DELETE') { server.sessions.splice(server.sessions.indexOf(s), 1); return ok(sessionList()); }
        if (!m[2] && method === 'GET') return ok(s);
        if (!m[2] && method === 'PUT') {
          if (body.name) s.name = body.name;
          if (body.board) {
            if (s.ended) return fail(409, 'That session is closed: open it to change it.');
            s.board = body.board;
          }
          return ok({ ok: true });
        }
        if (m[2] === 'end') { if (!s.ended) s.ended = iso(); return ok(sessionList()); }
        if (m[2] === 'open') {
          server.sessions.filter((x) => !x.ended && x !== s).forEach((x) => { x.ended = iso(); });
          if (s.ended) { s.ended = ''; s.reopened += 1; }
          return ok(sessionList());
        }
      }
      return fail(404, 'Not found');
    }));
  };
  return server;
}
