import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import SheetView from './SheetView.vue';
import me from '../test/fixtures/me.json';

const clone = () => structuredClone(me);

describe('SheetView', () => {
  it('shows the identity block and the name like the printed sheet', () => {
    const w = mount(SheetView, { props: { me: clone() } });
    expect(w.get('[data-test=name]').text()).toBe('Brenna');
    expect(w.text()).toContain('"Grimjaw"');
    const lines = w.findAll('.a-ident .line').map((l) => l.text());
    expect(lines).toEqual(expect.arrayContaining(['PlayerSebastián', 'KinHuman', 'AgeAdult', 'ProfessionFighter']));
    expect(w.get('.a-ident').text()).toContain('Child of the Wild');
  });

  it('shows the six attributes with their base chance and the derived numbers the server computed', () => {
    const w = mount(SheetView, { props: { me: clone() } });
    expect(w.findAll('[data-test=attribute]').map((a) => a.text())).toEqual(['15', '13', '12', '9', '10', '11']);
    expect(w.text()).toContain('base chance 6');
    expect(w.get('[data-test=movement]').text()).toBe('10');
    expect(w.get('[data-test=damage-str]').text()).toBe('+D4');
    expect(w.get('[data-test=damage-agl]').text()).toBe('–');
    expect(w.get('[data-test=carrying]').text()).toBe('3/8');
    expect(w.findAll('[data-test=meter-value]').map((m) => m.text())).toEqual(['10 / 13', '10 / 10']);
  });

  it('draws hit points as circles that go empty as they are lost', () => {
    const w = mount(SheetView, { props: { me: clone() } });
    const hp = w.get('.points.hp');
    expect(hp.findAll('.pip')).toHaveLength(13);
    expect(hp.findAll('.pip.gone')).toHaveLength(3);
    expect(w.get('.points.wp').findAll('.pip.gone')).toHaveLength(0);
  });

  it('lists every skill in the printed order, and only the trained ones on request', async () => {
    const w = mount(SheetView, { props: { me: clone() } });
    const names = () => w.findAll('.skill').map((r) => r.text());
    expect(names().some((t) => t.startsWith('12') && t.includes('Axes (STR)'))).toBe(true);
    expect(names().some((t) => t.includes('Acrobatics (AGL)'))).toBe(true);
    const heads = w.findAll('.skill-head').map((h) => h.text());
    expect(heads.indexOf('Skills')).toBeLessThan(heads.indexOf('Weapon skills'));
    await w.get('input[type=checkbox]').setValue(true);
    expect(names().some((t) => t.includes('Acrobatics'))).toBe(false);
    expect(names().some((t) => t.includes('Axes'))).toBe(true);
    await w.get('input[type=checkbox]').setValue(false);
    const levels = Object.fromEntries(w.findAll('.skill').map((r) => [r.text().replace(/^\d+\s*/, '').split(' (')[0], r.get('[data-test=skill-level]').text()]));
    expect(levels.Axes).toBe('12');
    expect(levels.Acrobatics).toBe('5');
  });

  it('shows each condition under its attribute, lit when active, and says what it does', () => {
    const data = clone();
    data.conditions.find((c) => c.name === 'Scared').active = true;
    const w = mount(SheetView, { props: { me: data } });
    expect(w.findAll('.cond')).toHaveLength(6);
    const on = w.findAll('.cond.on').map((c) => c.text());
    expect(on).toEqual(['Scared']);
    expect(w.get('.cond.on').element.closest('.gem').textContent).toContain('WIL');
    expect(w.text()).toContain('bane on WIL rolls');
  });

  it('opens abilities to their text, and shows names only for what is no longer loaded', () => {
    const data = clone();
    data.abilities.push({ key: 'gone/ability/x', kind: 'ability', name: 'Vanished Power', found: false });
    const w = mount(SheetView, { props: { me: data } });
    const veteran = w.findAll('details.entry').find((d) => d.text().includes('Veteran'));
    expect(veteran.text()).toContain('A hardened fighter.');
    expect(veteran.text()).toContain('Second paragraph.');
    const gone = w.findAll('details.entry').find((d) => d.text().includes('Vanished Power'));
    expect(gone.text()).toContain('no longer in the loaded content');
  });

  it('shows weapons with their stats, armor with its rating and the banes, numbered inventory lines and the coins', () => {
    const w = mount(SheetView, { props: { me: clone() } });
    const star = w.findAll('.weapon').find((x) => x.text().includes('Morningstar'));
    expect(star.text()).toContain('DamageD10');
    expect(w.get('[data-test=armor]').text()).toBe('Chainmail');
    expect(w.get('.a-armor .rating').text()).toBe('4');
    expect(w.get('.a-armor .bane').text()).toContain('Sneaking');
    expect(w.get('.a-helmet .bane').text()).toContain('Ranged attacks');
    expect(w.get('[data-test=helmet]').text()).toBe('None');
    const lines = w.findAll('.a-inv .line');
    expect(lines.length).toBeGreaterThanOrEqual(8);
    expect(lines[0].text()).toContain('Torch');
    expect(w.get('[data-test=silver]').text()).toBe('1');
    expect(w.get('[data-test=gold]').text()).toBe('0');
  });

  it('never interprets text as HTML', () => {
    const data = clone();
    data.about.notes = '<img src=x onerror="alert(1)"><b>bold</b>';
    const w = mount(SheetView, { props: { me: data } });
    expect(w.find('img').exists()).toBe(false);
    expect(w.find('b').exists() && w.find('.a-about b').exists()).toBe(false);
    expect(w.text()).toContain('<img src=x onerror="alert(1)">');
  });
});

// ---------------------------------------------------------------------------------------------------- editing
describe('SheetView editing', () => {
  beforeEach(() => vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] }));
  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  // A stand-in for the server: applies a set to the sheet's doc like the real one (attributes and coins merge, the rest replace).
  function fakeServer({ refuse } = {}) {
    let current = structuredClone(me);
    const patches = [];
    const f = vi.fn(async (url, opts = {}) => {
      if (opts.method === 'PATCH') {
        const { set } = JSON.parse(opts.body);
        patches.push(set);
        if (refuse) return { ok: false, status: refuse.status, json: async () => ({ error: refuse.error }) };
        current = structuredClone(current);
        for (const [key, value] of Object.entries(set)) {
          current.doc[key] = key === 'attributes' || key === 'coins' ? { ...current.doc[key], ...value } : value;
        }
        current.revision += 1;
        return { ok: true, status: 200, json: async () => structuredClone(current) };
      }
      if (url.startsWith('/api/content/abilities')) {
        return { ok: true, status: 200, json: async () => ({ entries: [{ key: 'core/ability/robust', name: 'Robust', subtitle: 'Heroic ability' }] }) };
      }
      return { ok: false, status: 404, json: async () => ({ error: 'no' }) };
    });
    vi.stubGlobal('fetch', f);
    return { f, patches, get current() { return current; } };
  }

  const mountEditable = (extra = {}) => mount(SheetView, { props: { me: clone(), token: 'good-token', ...extra } });
  const startEditing = async (w) => {
    await w.get('[data-test=edit]').trigger('click');
  };
  const settle = async () => {
    await vi.advanceTimersByTimeAsync(500);
    await flushPromises();
  };

  it('offers editing only to someone with a link, and not on a locked sheet', () => {
    expect(mount(SheetView, { props: { me: clone() } }).find('[data-test=edit]').exists()).toBe(false);
    expect(mountEditable().find('[data-test=edit]').exists()).toBe(true);
    const locked = mountEditable({ me: { ...clone(), locked: true } });
    expect(locked.find('[data-test=edit]').exists()).toBe(false);
    expect(locked.get('[data-test=locked]').text()).toContain('locked');
  });

  it('turns the sheet into inputs, and saves nothing until something changes', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await startEditing(w);
    expect(w.findAll('[data-test=attribute-input]')).toHaveLength(6);
    expect(w.find('[data-test=done]').exists()).toBe(true);
    await settle();
    expect(server.patches).toHaveLength(0);
  });

  it('saves a change a moment after it is made, and sends only the field that changed', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await startEditing(w);
    await w.findAll('.pip.tap')[6].trigger('click'); // the seventh circle: HP becomes 7
    expect(server.patches).toHaveLength(0); // (it waits a moment, in case the player keeps going)
    await settle();
    expect(server.patches).toEqual([{ hp: 7 }]);
    expect(w.emitted('updated')).toHaveLength(1);
    expect(w.emitted('updated')[0][0].doc.hp).toBe(7);
  });

  it('sends a burst of changes as one, key by key inside attributes and coins', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await startEditing(w);
    await w.findAll('[data-test=attribute-input]')[0].setValue('16');
    await w.get('[data-test=silver-input]').setValue('9');
    await w.get('textarea[aria-label=Notes]').setValue('Owes the innkeeper.');
    await settle();
    expect(server.patches).toEqual([{ attributes: { STR: 16 }, coins: { silver: 9 }, notes: 'Owes the innkeeper.' }]);
  });

  it('adds, changes and removes items, and toggles conditions and advancement marks', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await startEditing(w);
    await w.get('[data-test=add-item]').trigger('click');
    const rows = w.findAll('[data-test=item-input]');
    await rows[rows.length - 1].setValue('Rope');
    await w.find('button[aria-label="Remove Torch"]').trigger('click');
    await w.findAll('.gem button.cond').find((b) => b.text().includes('Scared')).trigger('click');
    await w.find('button[aria-label="Advancement mark for Acrobatics"]').trigger('click');
    await settle();
    const set = server.patches[0];
    expect(set.inventory.map((i) => i.name)).toEqual(['Flint & Tinder', 'Food rations', 'Rope']);
    expect(set.conditions).toEqual(['Scared']);
    expect(set.skills.find((s) => s.name === 'Acrobatics')).toMatchObject({ marked: true, level: 0, trained: false });
  });

  it('trains a skill and changes its level', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await startEditing(w);
    const row = w.findAll('.skill').find((r) => r.text().includes('Acrobatics'));
    await row.get('input[type=checkbox]').setValue(true);
    await settle();
    expect(server.patches[0].skills.find((s) => s.name === 'Acrobatics')).toMatchObject({ trained: true, level: 10 });
    await w.findAll('.skill').find((r) => r.text().includes('Acrobatics')).get('[data-test=skill-input]').setValue('14');
    await settle();
    expect(server.patches[1].skills.find((s) => s.name === 'Acrobatics').level).toBe(14);
  });

  it('adds an ability picked from the rules', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await startEditing(w);
    await w.findAll('.picker button').find((b) => b.text().includes('ability')).trigger('click');
    await vi.advanceTimersByTimeAsync(300);
    await flushPromises();
    await w.find('.picker-panel li button').trigger('click');
    await settle();
    expect(server.patches[0].abilities.map((a) => a.name)).toContain('Robust');
    expect(server.patches[0].abilities.find((a) => a.name === 'Robust').key).toBe('core/ability/robust');
  });

  it('flushes what is pending when the player presses Done', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await startEditing(w);
    await w.get('[data-test=gold-input]').setValue('5');
    await w.get('[data-test=done]').trigger('click');
    await flushPromises();
    expect(server.patches).toEqual([{ coins: { gold: 5 } }]);
    expect(w.find('[data-test=edit]').exists()).toBe(true);
  });

  it('shows the rule breaks in red: which part, what is wrong, and what the GM said', () => {
    const data = clone();
    data.issues = [
      { key: 'encumbrance', message: 'Carrying 11 items, the limit is 8', status: 'pending' },
      { key: 'attr:STR', message: 'Strength 19 is outside the 3 to 18 of the rules', status: 'rejected' },
    ];
    const w = mount(SheetView, { props: { me: data, token: 't' } });
    const boxes = w.findAll('[data-test=issue]');
    expect(boxes).toHaveLength(2);
    expect(boxes[0].text()).toContain('Rule check');
    expect(boxes[0].text()).toContain('your GM will look at it');
    expect(boxes[1].text()).toContain('Your GM says no');
    expect(boxes[1].classes()).toContain('rejected');
    expect(w.get('[data-test=carrying]').classes()).toContain('flagged');
    expect(w.findAll('.gem')[0].classes()).toContain('flagged');
    expect(w.findAll('.gem')[1].classes()).not.toContain('flagged');
    expect(mount(SheetView, { props: { me: clone() } }).find('[data-test=issues]').exists()).toBe(false);
  });

  it('when the server refuses a change, says why and goes back to what the server has', async () => {
    fakeServer({ refuse: { status: 400, error: 'hp must be a whole number' } });
    const w = mountEditable();
    await startEditing(w);
    await w.findAll('.pip.tap')[3].trigger('click');
    await settle();
    expect(w.get('[data-test=save-error]').text()).toContain('whole number');
    expect(w.findAll('.points.hp .pip:not(.gone)')).toHaveLength(me.hp.current);
  });

  it('leaves editing when the GM locks the sheet', async () => {
    fakeServer({ refuse: { status: 423, error: 'Your GM has locked your sheet.' } });
    const w = mountEditable();
    await startEditing(w);
    await w.get('[data-test=gold-input]').setValue('1');
    await settle();
    expect(w.find('[data-test=done]').exists()).toBe(false);
    expect(w.find('[data-test=edit]').exists()).toBe(false);
    expect(w.get('[data-test=locked]').text()).toContain('locked');
  });

  it('takes in a change the GM made while the player is editing, without losing what the player is typing', async () => {
    fakeServer();
    const w = mountEditable();
    await startEditing(w);
    const fresh = clone();
    fresh.doc.coins.gold = 30; // the GM
    fresh.doc.notes = 'from the GM';
    await w.setProps({ me: fresh });
    await flushPromises();
    expect(w.get('[data-test=gold-input]').element.value).toBe('30');
    expect(w.get('textarea[aria-label=Notes]').element.value).toBe('from the GM');
    await w.get('[data-test=silver-input]').setValue('12'); // the player, before the next poll
    const later = clone();
    later.doc.coins.gold = 31;
    later.doc.coins.silver = 99; // the GM again, on the same field the player is editing
    await w.setProps({ me: later });
    await flushPromises();
    expect(w.get('[data-test=gold-input]').element.value).toBe('31');
    expect(w.get('[data-test=silver-input]').element.value).toBe('12');
  });
});
