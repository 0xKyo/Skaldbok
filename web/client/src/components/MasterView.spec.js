import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import { ref } from 'vue';
import MasterView from './MasterView.vue';
import { useBoard } from '../board.js';
import { useCampaigns } from '../campaigns.js';
import { useSessions } from '../sessions.js';
import { fakeServer } from '../test/fakeSessions.js';

let server;

const PEOPLE = [{ id: 'brenna', name: 'Brenna', kin: 'Human', profession: 'Fighter' }, { id: 'garmander', name: 'Garmander', kin: 'Dwarf', profession: 'Mage' }];

beforeEach(() => {
  window.history.replaceState(null, '', '/');                      // the part open is in the address: every test starts on the Screen
  server = fakeServer();
  server.install();
  URL.createObjectURL = vi.fn(() => 'blob:x');
  URL.revokeObjectURL = vi.fn();
});
afterEach(() => vi.unstubAllGlobals());

async function mountMaster() {
  const screen = useBoard('gm');
  await screen.load();
  const sessions = useSessions('gm', (t) => screen.say(t));
  const campaigns = useCampaigns('gm', (t) => screen.say(t));
  const w = mount(MasterView, { props: { token: 'gm' }, global: { provide: { board: screen, sessions, campaigns, characters: ref(PEOPLE) } } });
  await flushPromises();
  return { w, screen, sessions, campaigns };
}

// what a dropdown shows now
const selectedText = (w, test) => w.get(`[data-test=${test}]`).element.selectedOptions[0].text;
const click = async (w, test) => { await w.get(`[data-test=${test}]`).trigger('click'); await flushPromises(); };
// opens the Campaigns part and starts a campaign (the first thing to do)
async function withCampaign(w) {
  await click(w, 'part-campaigns');
  await click(w, 'new-campaign');
}
async function withSession(w) {
  await withCampaign(w);
  await click(w, 'new-session');
}

describe('MasterView: the parts and the campaigns', () => {
  it('has two parts: the Master Screen (a board) and the Campaigns', async () => {
    const { w } = await mountMaster();
    expect(w.get('[data-test=part-screen]').classes()).toContain('active');
    expect(w.find('[data-test=board]').exists()).toBe(true);
    expect(w.find('[data-test=campaign-bar]').exists()).toBe(false);
    await click(w, 'part-campaigns');
    expect(w.get('[data-test=part-campaigns]').classes()).toContain('active');
    expect(w.get('[data-test=no-campaigns]').text()).toContain('No campaigns yet');
    expect(w.get('[data-test=campaigns-empty]').text()).toContain('A campaign is what you play');
    expect(w.find('[data-test=sessions-bar]').exists()).toBe(false);                      // no sessions without a campaign
    expect(w.find('[data-test=board]').exists()).toBe(false);
    expect(window.location.hash).toBe('#master/campaigns');
  });

  it('starts a campaign: named, chosen in a dropdown, with no sessions yet', async () => {
    const { w } = await mountMaster();
    await withCampaign(w);
    expect(selectedText(w, 'campaign-pick')).toBe('Campaign 1');
    expect(w.findAll('[data-test=campaign-pick] option').map((o) => o.text())).toEqual(['Campaign 1']);
    expect(w.get('[data-test=no-sessions]').text()).toContain('No sessions yet');
    expect(w.get('[data-test=sessions-empty]').text()).toContain('A session is a board for one game');
    expect(w.find('[data-test=new-session]').exists()).toBe(true);
  });

  it('renames a campaign, and chooses between campaigns in the dropdown', async () => {
    const { w, campaigns } = await mountMaster();
    await withCampaign(w);
    await click(w, 'rename-campaign');
    expect(w.find('[data-test=campaign-pick]').exists()).toBe(false);                       // the name is written in place of the dropdown
    expect(w.get('[data-test=campaign-name]').element.value).toBe('Campaign 1');
    await w.get('[data-test=campaign-name]').setValue('The Misty Vale');
    await w.get('[data-test=campaign-name]').trigger('keydown', { key: 'Enter' });
    await flushPromises();
    expect(w.find('[data-test=campaign-name]').exists()).toBe(false);                       // and the dropdown is back
    expect(server.campaigns[0].name).toBe('The Misty Vale');
    await click(w, 'new-campaign');
    expect(selectedText(w, 'campaign-pick')).toBe('Campaign 2');           // the new one is the one shown
    await w.get('[data-test=campaign-pick]').setValue('c1');
    await flushPromises();
    expect(selectedText(w, 'campaign-pick')).toBe('The Misty Vale');
    expect(campaigns.list.value.map((c) => c.name)).toEqual(['Campaign 2', 'The Misty Vale']);
  });

  it('assigns the characters to a campaign with a dropdown of check boxes', async () => {
    const { w } = await mountMaster();
    await withCampaign(w);
    expect(w.get('[data-test=characters-summary]').text()).toBe('Nobody yet');
    expect(w.find('[data-test=characters-panel]').exists()).toBe(false);
    await click(w, 'characters-button');
    expect(w.findAll('[data-test=character-row]').map((r) => r.text())).toEqual(['Brenna · Human Fighter', 'Garmander · Dwarf Mage']);
    await w.findAll('[data-test=character-row] input')[0].setValue(true);
    await flushPromises();
    expect(server.campaigns[0].characters).toEqual(['brenna']);
    expect(w.get('[data-test=characters-summary]').text()).toBe('Brenna');
    await w.findAll('[data-test=character-row] input')[1].setValue(true);
    await flushPromises();
    expect(w.get('[data-test=characters-summary]').text()).toBe('Brenna, Garmander');
    await w.findAll('[data-test=character-row] input')[0].setValue(false);
    await flushPromises();
    expect(server.campaigns[0].characters).toEqual(['garmander']);
    document.body.click();                                                                  // a click elsewhere closes the dropdown
    await flushPromises();
    expect(w.find('[data-test=characters-panel]').exists()).toBe(false);
  });

  it('deleting a campaign asks first, counts its sessions, and takes them with it', async () => {
    const { w, sessions } = await mountMaster();
    await withSession(w);
    await click(w, 'new-campaign');
    await w.get('[data-test=campaign-pick]').setValue('c1');
    await flushPromises();
    await click(w, 'delete-campaign');
    expect(w.get('[data-test=delete-campaign-confirm]').text()).toContain('Campaign 1');
    expect(w.get('[data-test=delete-campaign-confirm]').text()).toContain('1 session');
    await click(w, 'confirm-cancel');
    expect(server.campaigns).toHaveLength(2);
    await click(w, 'delete-campaign');
    await click(w, 'delete-campaign-yes');
    expect(server.campaigns.map((c) => c.name)).toEqual(['Campaign 2']);
    expect(server.sessions).toHaveLength(0);
    expect(sessions.list.value).toHaveLength(0);
    expect(sessions.active.value).toBe('');
    expect(selectedText(w, 'campaign-pick')).toBe('Campaign 2');           // the other one is shown
  });
});

describe('MasterView: one line', () => {
  it('the campaign, who plays it, its session and what can be done are in one bar', async () => {
    const { w } = await mountMaster();
    await withSession(w);
    const bar = w.get('[data-test=campaign-bar]');
    for (const test of ['campaign-pick', 'characters-button', 'session-pick', 'session-dates', 'end-session', 'new-session', 'new-campaign', 'delete-campaign', 'delete-session']) {
      expect(bar.find(`[data-test=${test}]`).exists(), test).toBe(true);
    }
    expect(w.findAll('.bar')).toHaveLength(1);
  });

  it('Esc leaves a name as it was', async () => {
    const { w } = await mountMaster();
    await withCampaign(w);
    await click(w, 'rename-campaign');
    await w.get('[data-test=campaign-name]').setValue('Something else');
    await w.get('[data-test=campaign-name]').trigger('keydown', { key: 'Escape' });
    await flushPromises();
    expect(selectedText(w, 'campaign-pick')).toBe('Campaign 1');
    expect(server.campaigns[0].name).toBe('Campaign 1');
  });
});

describe('MasterView: the sessions of a campaign', () => {
  it('starts a session in the campaign: named, dated in the list, active, with a board of its own', async () => {
    const { w } = await mountMaster();
    await withSession(w);
    expect(selectedText(w, 'session-pick')).toContain('Session 1');
    expect(w.get('[data-test=session-dates]').text()).toBe('Active');
    expect(w.get('[data-test=session-pick] option').text()).toMatch(/Session 1 · [A-Z][a-z]{2} [0-9]{1,2}, [0-9]{4}$/);   // the day only, in the list: Oct 2, 2026
    expect(w.find('[data-test=end-session]').exists()).toBe(true);
    expect(w.find('[data-test=board]').exists()).toBe(true);
    expect(w.get('[data-test=empty]').text()).toContain('This session is empty');
    expect(w.get('[data-test=campaign-pick]').text()).toContain('Campaign 1');
    expect(server.sessions[0].campaign).toBe('c1');
  });

  it('End Session asks first, closes the session with its date, and leaves none active', async () => {
    const { w, sessions } = await mountMaster();
    await withSession(w);
    await click(w, 'end-session');
    expect(w.get('[data-test=end-confirm]').text()).toContain('Session 1');
    expect(sessions.active.value).not.toBe('');                                            // nothing happens until it is confirmed
    await click(w, 'end-yes');
    expect(sessions.active.value).toBe('');
    expect(w.get('[data-test=session-dates]').text()).toBe('Closed');
    expect(w.find('[data-test=end-session]').exists()).toBe(false);
    expect(w.find('[data-test=open-session]').exists()).toBe(true);
    expect(w.get('[data-test=closed-note]').text()).toContain('Closed session');
    expect(w.find('[data-test=add-note]').exists()).toBe(false);                           // a closed session cannot be changed
  });

  it('Open Session makes a closed one the active one again, and it can be changed', async () => {
    const { w, sessions } = await mountMaster();
    await withSession(w);
    const id = sessions.active.value;
    await click(w, 'end-session');
    await click(w, 'end-yes');
    await click(w, 'open-session');
    expect(sessions.active.value).toBe(id);
    expect(w.get('[data-test=session-dates]').text()).toBe('Active');
    expect(w.find('[data-test=add-note]').exists()).toBe(true);
    expect(w.find('[data-test=closed-note]').exists()).toBe(false);
  });

  it('a new session while one is active asks first, because it ends the other', async () => {
    const { w, sessions } = await mountMaster();
    await withSession(w);
    const first = sessions.active.value;
    await click(w, 'new-session');
    expect(w.get('[data-test=new-confirm]').text()).toContain('ends the active session first');
    expect(sessions.active.value).toBe(first);
    await click(w, 'new-yes');
    expect(sessions.active.value).not.toBe(first);
    expect(sessions.find(first).ended).toBeTruthy();
    expect(w.findAll('[data-test=session-pick] option')).toHaveLength(2);
    expect(selectedText(w, 'session-pick')).toContain('Session 2');            // the new one is the one shown
  });

  it('deletes a session: it asks first, and the one that was looked at gives way to another', async () => {
    const { w, sessions } = await mountMaster();
    await withSession(w);
    await click(w, 'new-session');
    await click(w, 'new-yes');
    expect(sessions.list.value).toHaveLength(2);
    await click(w, 'delete-session');
    expect(w.get('[data-test=delete-confirm]').text()).toContain('Session 2');
    await click(w, 'confirm-cancel');
    expect(sessions.list.value).toHaveLength(2);
    await click(w, 'delete-session');
    await click(w, 'delete-yes');
    expect(sessions.list.value.map((s) => s.name)).toEqual(['Session 1']);
    expect(server.sessions).toHaveLength(1);
    expect(sessions.active.value).toBe('');                                               // the active one was deleted: none is active
    expect(selectedText(w, 'session-pick')).toContain('Session 1');            // the other one is shown, closed
    expect(w.get('[data-test=campaign-pick]').text()).toContain('Campaign 1');
    await click(w, 'delete-session');
    await click(w, 'delete-yes');
    expect(w.get('[data-test=no-sessions]').text()).toContain('No sessions yet');
    expect(w.find('[data-test=delete-session]').exists()).toBe(false);
    expect(w.find('[data-test=new-session]').exists()).toBe(true);
  });

  it('each campaign has its own sessions; the dropdown of sessions shows only those of the campaign', async () => {
    const { w, sessions } = await mountMaster();
    await withSession(w);                                                                   // Campaign 1: Session 1
    await click(w, 'new-campaign');                                                         // Campaign 2
    expect(w.find('[data-test=session-pick]').exists()).toBe(false);                        // none yet in this one
    await click(w, 'new-session');                                                          // asks: it ends the active one (of Campaign 1)
    await click(w, 'new-yes');
    expect(w.findAll('[data-test=session-pick] option').map((o) => o.text().slice(0, 18))).toEqual(['● Session 1 · Oct ']);
    expect(server.sessions.map((s) => s.campaign)).toEqual(['c1', 'c2']);
    expect(sessions.find('s1').ended).toBeTruthy();
    await w.get('[data-test=campaign-pick]').setValue('c1');
    await flushPromises();
    expect(w.findAll('[data-test=session-pick] option')).toHaveLength(1);
    expect(w.get('[data-test=session-dates]').text()).toBe('Closed');                       // the one of Campaign 1 was closed by starting the other
  });

  it('chooses a session from the list and renames it', async () => {
    const { w, sessions } = await mountMaster();
    await withSession(w);
    await click(w, 'new-session');
    await click(w, 'new-yes');
    await w.get('[data-test=session-pick]').setValue('s1');
    await flushPromises();
    expect(selectedText(w, 'session-pick')).toContain('Session 1');
    expect(w.get('[data-test=session-dates]').text()).toBe('Closed');
    await w.get('[data-test=session-pick]').setValue('s2');
    await click(w, 'rename-session');
    await w.get('[data-test=session-name]').setValue('The Isle of Mist');
    await w.get('[data-test=session-name]').trigger('keydown', { key: 'Enter' });
    await flushPromises();
    expect(server.sessions[1].name).toBe('The Isle of Mist');
    expect(w.get('[data-test=session-pick]').text()).toContain('The Isle of Mist');
    expect(sessions.find('s2').name).toBe('The Isle of Mist');
  });
});

describe('MasterView: sending from the Screen', () => {
  it('sends a copy to the active session, which the Screen names with its campaign', async () => {
    const { w, screen, sessions } = await mountMaster();
    screen.addNote('yellow', 'They fought the ogre and won. The loot: a silver ring.');
    await flushPromises();
    expect(w.get('[data-test=session-chip]').text()).toContain('No active session');
    expect(w.get('[data-test=send]').attributes('disabled')).toBeDefined();               // nowhere to send it yet
    await withSession(w);
    await click(w, 'part-screen');
    expect(w.get('[data-test=session-chip]').text()).toBe('→ Campaign 1 · Session 1');
    expect(w.get('[data-test=send]').attributes('disabled')).toBeUndefined();
    await click(w, 'send');
    expect(screen.items.value).toHaveLength(1);                                           // it stays on the Screen
    expect(server.sessions[0].board.items).toHaveLength(1);
    expect(server.sessions[0].board.items[0].text).toContain('silver ring');
    expect(screen.notice.value).toContain('Sent to “Session 1”');
    await click(w, 'part-campaigns');
    expect(w.get('[data-test=note-text]').element.value).toContain('silver ring');         // and it is there, in the session
    expect(w.find('[data-test=send]').exists()).toBe(false);                               // nothing is sent from a session
    expect(sessions.find('s1').items).toBe(1);
  });

  it('a picture can be sent too, and a pinned thing', async () => {
    const { w, screen } = await mountMaster();
    screen.addImage({ adventure: 'mistyvale', file: 'images/people/hardy.png' }, { title: 'Hardy' });
    screen.pin({ type: 'table', key: 'table:Loot', title: 'Loot', data: { title: 'Loot', dice: 'D6', columns: ['ITEM'], rows: [{ roll: '1', cells: ['A ring'] }] } });
    await withSession(w);
    await click(w, 'part-screen');
    const sends = w.findAll('[data-test=send]');
    expect(sends).toHaveLength(2);
    await sends[0].trigger('click');
    await flushPromises();
    await sends[1].trigger('click');
    await flushPromises();
    const kinds = server.sessions[0].board.items.map((i) => i.kind).sort();
    expect(kinds).toEqual(['image', 'pin']);
    expect(server.sessions[0].board.items.find((i) => i.kind === 'image').image).toEqual({ adventure: 'mistyvale', file: 'images/people/hardy.png' });
  });
});
