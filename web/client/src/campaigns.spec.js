import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { useCampaigns } from './campaigns.js';
import { fakeServer } from './test/fakeSessions.js';

let server;
let said;

beforeEach(() => {
  server = fakeServer();
  server.install();
  said = [];
});
afterEach(() => vi.unstubAllGlobals());

const open = async () => {
  const campaigns = useCampaigns('gm', (t) => said.push(t));
  await campaigns.refresh();
  return campaigns;
};

describe('useCampaigns', () => {
  it('starts with no campaigns', async () => {
    const campaigns = await open();
    expect(campaigns.list.value).toEqual([]);
    expect(campaigns.loaded.value).toBe(true);
  });

  it('makes a campaign with a name of its own, newest first', async () => {
    const campaigns = await open();
    const a = await campaigns.create();
    const b = await campaigns.create('The Misty Vale');
    expect(campaigns.list.value.map((c) => c.name)).toEqual(['The Misty Vale', 'Campaign 1']);
    expect(campaigns.find(b).characters).toEqual([]);
    expect(campaigns.find(a).sessions).toBe(0);
    expect(said.at(-1)).toContain('Started the campaign “The Misty Vale”');
  });

  it('renames a campaign (not to nothing, not to the same)', async () => {
    const campaigns = await open();
    const id = await campaigns.create();
    expect(await campaigns.rename(id, '  Dragonbane  ')).toBe(true);
    expect(campaigns.find(id).name).toBe('Dragonbane');
    expect(await campaigns.rename(id, 'Dragonbane')).toBe(false);
    expect(await campaigns.rename(id, '   ')).toBe(false);
  });

  it('assigns characters, once each', async () => {
    const campaigns = await open();
    const id = await campaigns.create();
    expect(await campaigns.setCharacters(id, ['brenna', 'garmander', 'brenna'])).toBe(true);
    expect(campaigns.find(id).characters).toEqual(['brenna', 'garmander']);
    expect(server.campaigns[0].characters).toEqual(['brenna', 'garmander']);
  });

  it('deletes a campaign and answers with what is left, sessions included', async () => {
    const campaigns = await open();
    const a = await campaigns.create();
    const b = await campaigns.create();
    server.sessions.push({ id: 's1', campaign: a, name: 'S', created: 'x', ended: '', reopened: 0, board: { items: [], arrows: [], view: {} } });
    const after = await campaigns.remove(a);
    expect(after.campaigns.map((c) => c.id)).toEqual([b]);
    expect(after.sessions).toEqual([]);
    expect(campaigns.list.value).toHaveLength(1);
    expect(said.at(-1)).toContain('Deleted “Campaign 1”');
    expect(await campaigns.remove('nope')).toBeNull();
  });
});
