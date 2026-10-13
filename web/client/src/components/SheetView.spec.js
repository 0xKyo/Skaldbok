import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import SheetView from './SheetView.vue';
import me from '../test/fixtures/me.json';

const clone = () => structuredClone(me);
// meReadonly: strip the editable doc so the sheet renders in display mode (simulates a viewer without a token)
// a number is typed, and handed over when the box is left
const typeNumber = async (field, text) => {
  field.element.value = text;
  await field.trigger('input');
  await field.trigger('change');
};
const meReadonly = () => { const m = clone(); delete m.doc; return m; };

describe('SheetView', () => {
  it('shows the identity block and the name', () => {
    const w = mount(SheetView, { props: { me: meReadonly() } });
    expect(w.get('[data-test=name]').text()).toBe('Brenna');
    const lines = w.findAll('.a-ident .line').map((l) => l.text());
    expect(lines.some((t) => t.includes('Sebastián'))).toBe(true);
    expect(lines.some((t) => t.includes('Human'))).toBe(true);
    expect(w.findAll('.a-appear .line').some((l) => l.text().includes('Fighter'))).toBe(true);
    expect(w.text()).toContain('Child of the Wild');
  });

  it('shows the six attributes with their base chance and the derived numbers the server computed', () => {
    const w = mount(SheetView, { props: { me: meReadonly() } });
    expect(w.findAll('[data-test=attribute]').map((a) => a.text())).toEqual(['15', '13', '12', '9', '10', '11']);
    expect(w.text()).toContain('base chance 6');
    expect(w.get('[data-test=movement]').text()).toBe('10');
    expect(w.get('[data-test=damage-strength]').text()).toBe('+D4');
    expect(w.get('[data-test=damage-agility]').text()).toBe('–');
    expect(w.get('[data-test=carrying]').text()).toBe('3/8');
    expect(w.findAll('[data-test=meter-value]').map((m) => m.text())).toEqual(['10 / 13', '10 / 10']);
  });

  it('draws hit points as circles that fill as they are lost: all empty when whole', () => {
    const w = mount(SheetView, { props: { me: meReadonly() } });
    const hp = w.get('.points.hp');
    expect(hp.findAll('.pip')).toHaveLength(13);
    expect(hp.findAll('.pip:not(.gone)')).toHaveLength(3);
    expect(w.get('.points.wp').findAll('.pip:not(.gone)')).toHaveLength(0);
  });

  it('lists every skill in the printed order', () => {
    const w = mount(SheetView, { props: { me: meReadonly() } });
    const skills = w.findAll('.skill');
    expect(skills.some((r) => r.text().includes('Axes (STR)'))).toBe(true);
    expect(skills.some((r) => r.text().includes('Acrobatics (AGL)'))).toBe(true);
    const heads = w.findAll('.skill-head').map((h) => h.text());
    expect(heads.indexOf('Skills')).toBeLessThan(heads.indexOf('Weapon skills'));
    const levels = Object.fromEntries(
      skills
        .filter((r) => r.find('[data-test=skill-level]').exists())
        .map((r) => [r.find('.skill-link').text().split(' (')[0], r.get('[data-test=skill-level]').text()]),
    );
    expect(levels.Axes).toBe('12');
    expect(levels.Acrobatics).toBe('5');
  });

  it('shows each condition under its attribute, lit when active, and says what it does', () => {
    const data = meReadonly();
    data.conditions.find((c) => c.name === 'Scared').active = true;
    const w = mount(SheetView, { props: { me: data } });
    expect(w.findAll('.cond')).toHaveLength(6);
    const on = w.findAll('.cond.on').map((c) => c.text());
    expect(on).toEqual(['Scared']);
    expect(w.get('.cond.on').element.closest('.gem').textContent).toContain('WIL');
    expect(w.get('.cond.on').attributes('title')).toContain('WIL');
  });

  it('shows abilities as rule links', () => {
    const w = mount(SheetView, { props: { me: meReadonly() } });
    expect(w.findAll('.entry-link').some((b) => b.text().includes('Veteran'))).toBe(true);
    expect(w.findAll('.entry-link').some((b) => b.text().includes('Heroic ability'))).toBe(true);
  });

  it('shows weapons with their stats, armor with its rating, and coins', () => {
    const w = mount(SheetView, { props: { me: meReadonly() } });
    const star = w.findAll('.weapon').find((x) => x.text().includes('Morningstar'));
    expect(star.text()).toContain('DamageD10');
    expect(w.get('[data-test=armor]').text()).toBe('Chainmail');
    expect(w.get('.a-armor .rating').text()).toBe('4');
    expect(w.get('.a-armor .bane').text()).toContain('Sneaking');
    expect(w.get('[data-test=helmet]').text()).toBe('None');
    expect(w.get('[data-test=silver]').text()).toBe('1');
    expect(w.get('[data-test=gold]').text()).toBe('0');
  });

  it('never interprets text as HTML', () => {
    const data = meReadonly();
    data.about.notes = '<img src=x onerror="alert(1)"><b>bold</b>';
    const w = mount(SheetView, { props: { me: data } });
    expect(w.find('.a-about img').exists()).toBe(false);
    expect(w.find('.a-about b').exists()).toBe(false);
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
  const settle = async () => {
    await vi.advanceTimersByTimeAsync(500);
    await flushPromises();
  };

  it('shows edit controls with a personal link, and not on a locked or doc-less sheet', () => {
    // without doc (display mode): no edit inputs
    expect(mount(SheetView, { props: { me: meReadonly() } }).findAll('[data-test=attribute-input]')).toHaveLength(0);
    // with doc and token: edit mode is always on
    expect(mountEditable().findAll('[data-test=attribute-input]')).toHaveLength(6);
    const locked = mountEditable({ me: { ...clone(), locked: true } });
    expect(locked.findAll('[data-test=attribute-input]')).toHaveLength(0);
    expect(locked.get('[data-test=locked]').text()).toContain('locked');
  });

  it('shows edit inputs and saves nothing until something changes', async () => {
    const server = fakeServer();
    const w = mountEditable();
    expect(w.findAll('[data-test=attribute-input]')).toHaveLength(6);
    await settle();
    expect(server.patches).toHaveLength(0);
  });

  it('saves a change a moment after it is made, and sends only the field that changed', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await w.findAll('.pip.tap')[5].trigger('click'); // the sixth circle is marked: 6 points lost, HP becomes 7
    expect(server.patches).toHaveLength(0); // (it waits a moment, in case the player keeps going)
    await settle();
    expect(server.patches).toEqual([{ hp: 7 }]);
    expect(w.emitted('updated')).toHaveLength(1);
    expect(w.emitted('updated')[0][0].doc.hp).toBe(7);
  });

  it('sends a burst of changes as one, key by key inside attributes and coins', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await typeNumber(w.findAll('[data-test=attribute-input]')[0], '16');
    await typeNumber(w.get('[data-test=silver-input]'), '9');
    await w.get('textarea[aria-label=Notes]').setValue('Owes the innkeeper.');
    await settle();
    expect(server.patches).toEqual([{ attributes: { strength: 16 }, coins: { silver: 9 }, notes: 'Owes the innkeeper.' }]);
  });

  it('adds, changes and removes items, and toggles conditions and advancement marks', async () => {
    const server = fakeServer();
    const w = mountEditable();
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

  it('changes a skill level', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await typeNumber(w.findAll('.skill').find((r) => r.text().includes('Acrobatics')).get('[data-test=skill-input]'), '14');
    await settle();
    expect(server.patches[0].skills.find((s) => s.name === 'Acrobatics').level).toBe(14);
  });

  it('adds an ability picked from the rules', async () => {
    const server = fakeServer();
    const w = mountEditable();
    await w.findAll('.picker button').find((b) => b.text().includes('ability')).trigger('click');
    await vi.advanceTimersByTimeAsync(300);
    await flushPromises();
    await w.find('.picker-panel li button').trigger('click');
    await settle();
    expect(server.patches[0].abilities.map((a) => a.name)).toContain('Robust');
    expect(server.patches[0].abilities.find((a) => a.name === 'Robust').key).toBe('core/ability/robust');
  });

  it('shows the rule breaks in red: which part, what is wrong, and what the GM said', () => {
    const data = clone();
    data.issues = [
      { key: 'encumbrance', message: 'Carrying 11 items, the limit is 8', status: 'pending' },
      { key: 'attr:strength', message: 'Strength 19 is outside the 3 to 18 of the rules', status: 'rejected' },
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
    await w.findAll('.pip.tap')[3].trigger('click');
    await settle();
    expect(w.get('[data-test=save-error]').text()).toContain('whole number');
    expect(w.findAll('.points.hp .pip:not(.gone)')).toHaveLength(me.hp.max - me.hp.current);
  });

  const dying = () => {
    const m = clone();
    m.hp.current = 0;
    m.doc.hp = 0;
    return m;
  };

  it('at 0 hit points the bar gives way to the death rolls: three successes bring it back, three failures are death', async () => {
    const server = fakeServer();
    const w = mount(SheetView, { props: { me: dying(), token: 'good-token' } });
    expect(w.find('[data-test=death-rolls]').exists()).toBe(true);
    expect(w.find('.points.hp .now').exists()).toBe(false);
    const successes = () => w.findAll('[data-test=death-successes] .pip:not(.gone)').length;
    const failures = () => w.findAll('[data-test=death-failures] .pip:not(.gone)').length;
    await w.findAll('[data-test=death-successes] .pip.tap')[0].trigger('click');
    await w.findAll('[data-test=death-failures] .pip.tap')[1].trigger('click');
    expect([successes(), failures()]).toEqual([1, 2]);
    await w.findAll('[data-test=death-failures] .pip.tap')[1].trigger('click');          // tapping the last marked box clears it
    expect(failures()).toBe(1);
    await w.findAll('[data-test=death-successes] .pip.tap')[2].trigger('click');
    expect(w.find('[data-test=death-rolls]').exists()).toBe(false);                      // three successes: the bar is back, still at 0
    expect(w.find('.points.hp').exists()).toBe(true);
    await settle();
    expect(server.patches.at(-1)).toMatchObject({ death_successes: 3 });
  });

  it('three failures are death, and Undo takes the last one back; any hit points start the rolls over', async () => {
    fakeServer();
    const w = mount(SheetView, { props: { me: dying(), token: 'good-token' } });
    await w.findAll('[data-test=death-failures] .pip.tap')[2].trigger('click');
    expect(w.get('[data-test=dead]').text()).toContain('Dead');
    await w.get('[data-test=undo-death]').trigger('click');
    expect(w.find('[data-test=dead]').exists()).toBe(false);
    expect(w.findAll('[data-test=death-failures] .pip:not(.gone)')).toHaveLength(2);
    await w.findAll('[data-test=death-successes] .pip.tap')[2].trigger('click');          // stable: the bar is back
    await w.findAll('.points.hp .pip.tap')[0].trigger('click');                            // one point lost of 13: 12 left
    expect(w.find('[data-test=death-rolls]').exists()).toBe(false);
    await settle();
    expect(w.emitted('updated')?.at(-1)?.[0].doc ?? {}).toMatchObject({ hp: 12, death_successes: 0, death_failures: 0 });
  });

  it('has the Round Rest and the Stretch Rest to tick and untick, saved as they are', async () => {
    const server = fakeServer();
    const w = mountEditable();
    const round = w.get('[data-test=round-rest]');
    const stretch = w.get('[data-test=stretch-rest]');
    expect([round.element.checked, stretch.element.checked]).toEqual([false, false]);
    await round.setValue(true);
    await settle();
    expect(server.patches.at(-1)).toEqual({ round_rest: true });
    await round.setValue(false);
    await stretch.setValue(true);
    await settle();
    expect(server.patches.at(-1)).toEqual({ round_rest: false, stretch_rest: true });
    expect(w.text()).toContain('Round Rest');
  });

  it('shows the rests taken, without letting a viewer change them', () => {
    const m = meReadonly();
    m.rest = { round: true, stretch: false };
    const w = mount(SheetView, { props: { me: m } });
    expect(w.get('[data-test=round-rest]').element.checked).toBe(true);
    expect(w.get('[data-test=round-rest]').element.disabled).toBe(true);
    expect(w.get('[data-test=stretch-rest]').element.checked).toBe(false);
  });

  it('leaves editing when the GM locks the sheet', async () => {
    fakeServer({ refuse: { status: 423, error: 'Your GM has locked your sheet.' } });
    const w = mountEditable();
    await typeNumber(w.get('[data-test=gold-input]'), '1');
    await settle();
    expect(w.findAll('[data-test=attribute-input]')).toHaveLength(0);
    expect(w.get('[data-test=locked]').text()).toContain('locked');
  });

  it('takes in a change the GM made while the player is editing, without losing what the player is typing', async () => {
    fakeServer();
    const w = mountEditable();
    const fresh = clone();
    fresh.doc.coins.gold = 30; // the GM
    fresh.doc.notes = 'from the GM';
    await w.setProps({ me: fresh });
    await flushPromises();
    expect(w.get('[data-test=gold-input]').element.value).toBe('30');
    expect(w.get('textarea[aria-label=Notes]').element.value).toBe('from the GM');
    await typeNumber(w.get('[data-test=silver-input]'), '12'); // the player, before the next poll
    const later = clone();
    later.doc.coins.gold = 31;
    later.doc.coins.silver = 99; // the GM again, on the same field the player is editing
    await w.setProps({ me: later });
    await flushPromises();
    expect(w.get('[data-test=gold-input]').element.value).toBe('31');
    expect(w.get('[data-test=silver-input]').element.value).toBe('12');
  });
});

describe('SheetView weapon damage', () => {
  it('shows the damage bonus next to a weapon\'s damage: STR for a melee weapon, AGL for a ranged one', () => {
    const m = meReadonly();
    m.derived.damageBonus = [{ key: 'strength', short: 'STR', value: 'D4' }, { key: 'agility', short: 'AGL', value: 'D6' }];
    m.equipment.weapons[0].ranged = false;
    m.equipment.weapons[1].ranged = true;
    const w = mount(SheetView, { props: { me: m } });
    const bonuses = w.findAll('[data-test=weapon-damage-bonus]');
    expect(bonuses.map((b) => b.text())).toEqual(['+D4', '+D6']);
    expect(bonuses[0].attributes('title')).toBe('Damage bonus STR');
    expect(bonuses[1].attributes('title')).toBe('Damage bonus AGL');
  });

  it('shows nothing when the character has no damage bonus of that kind', () => {
    const m = meReadonly();
    m.derived.damageBonus = [{ key: 'strength', short: 'STR', value: '' }, { key: 'agility', short: 'AGL', value: '' }];
    const w = mount(SheetView, { props: { me: m } });
    expect(w.find('[data-test=weapon-damage-bonus]').exists()).toBe(false);
  });
});

describe('SheetView tabs for a phone', () => {
  it('is tabs on a phone: stats, skills, abilities, gear and details, opening on the stats', async () => {
    const w = mount(SheetView, { props: { me: meReadonly() } });
    const tabs = w.findAll('[data-test=sheet-tabs] button');
    expect(tabs.map((t) => t.text())).toEqual(['Stats', 'Skills', 'Abilities', 'Gear', 'Details']);
    expect(w.get('.sheet').attributes('data-tab')).toBe('stats');
    expect(w.get('[data-test=sheet-tab-stats]').attributes('aria-pressed')).toBe('true');
    await w.get('[data-test=sheet-tab-gear]').trigger('click');
    expect(w.get('.sheet').attributes('data-tab')).toBe('gear');
    expect(w.get('[data-test=sheet-tab-gear]').attributes('aria-pressed')).toBe('true');
    expect(w.get('[data-test=sheet-tab-stats]').attributes('aria-pressed')).toBe('false');
    await w.get('[data-test=sheet-tab-skills]').trigger('click');
    expect(w.get('.sheet').attributes('data-tab')).toBe('skills');
    await w.get('[data-test=sheet-tab-abilities]').trigger('click');
    expect(w.get('.sheet').attributes('data-tab')).toBe('abilities');
    await w.get('[data-test=sheet-tab-details]').trigger('click');
    expect(w.get('.sheet').attributes('data-tab')).toBe('details');
  });
});

describe('SheetView worn banes', () => {
  it('applies the banes of the worn armor and helmet by itself: on the skills, on the item and on ranged attacks', () => {
    const m = meReadonly();
    m.equipment.helmet = { name: 'Great Helm', count: 1, note: '', stats: [], banes: ['Awareness', 'Ranged attacks'], ranged: false, description: '' };
    m.equipment.weapons[0].ranged = true;
    const w = mount(SheetView, { props: { me: m } });
    expect(w.get('[data-test=armor-bane]').text()).toContain('Evade · Sneaking');
    expect(w.get('[data-test=helmet-bane]').text()).toContain('Awareness');
    expect(w.get('[data-test=helmet-bane]').text()).not.toContain('Ranged attacks');            // not written: the ranged weapons are outlined instead
    const flaggedSkills = w.findAll('.skill.flagged').map((s) => s.text());
    for (const name of ['Evade', 'Awareness']) expect(flaggedSkills.some((t) => t.includes(name))).toBe(true);
    expect(flaggedSkills.some((t) => t.includes('Acrobatics'))).toBe(false);
    expect(w.find('[data-test=weapon-bane]').exists()).toBe(false);
    expect(w.get('[data-test=weapon]').classes()).toContain('flagged');
  });

  it('shows no bane line and flags nothing extra when nothing worn has one', () => {
    const m = meReadonly();
    m.equipment.armor = { ...m.equipment.armor, banes: [] };
    const w = mount(SheetView, { props: { me: m } });
    expect(w.find('[data-test=armor-bane]').exists()).toBe(false);
    expect(w.find('[data-test=weapon-bane]').exists()).toBe(false);
  });
});
