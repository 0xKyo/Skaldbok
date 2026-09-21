import { describe, expect, it } from 'vitest';
import { adopt, clone, diffDoc, issueFor } from './sheetdoc.js';

const doc = () => ({
  hp: 10,
  notes: 'a',
  attributes: { STR: 15, CON: 13 },
  coins: { gold: 0, silver: 3, copper: 0 },
  inventory: [{ name: 'Torch' }],
  armor: null,
});

describe('diffDoc', () => {
  it('is empty when nothing changed', () => {
    expect(diffDoc(doc(), clone(doc()))).toEqual({});
  });

  it('holds whole values, except attributes and coins, which go key by key', () => {
    const draft = doc();
    draft.hp = 7;
    draft.attributes.STR = 16;
    draft.coins.gold = 2;
    draft.inventory.push({ name: 'Rope' });
    draft.armor = { name: 'Leather' };
    expect(diffDoc(doc(), draft)).toEqual({
      hp: 7,
      attributes: { STR: 16 },
      coins: { gold: 2 },
      inventory: [{ name: 'Torch' }, { name: 'Rope' }],
      armor: { name: 'Leather' },
    });
  });

  it('does not share objects with the draft', () => {
    const draft = doc();
    draft.inventory.push({ name: 'Rope' });
    const set = diffDoc(doc(), draft);
    draft.inventory[1].name = 'changed later';
    expect(set.inventory[1].name).toBe('Rope');
  });
});

describe('adopt', () => {
  it('takes what the server says where the player has changed nothing', () => {
    const base = doc();
    const server = doc();
    server.hp = 4;
    server.attributes.CON = 14;
    const next = adopt(clone(base), base, server);
    expect(next.hp).toBe(4);
    expect(next.attributes).toEqual({ STR: 15, CON: 14 });
  });

  it('keeps what the player is in the middle of, and takes the rest', () => {
    const base = doc();
    const draft = doc();
    draft.hp = 9; // the player changed this
    draft.attributes.STR = 16; // and this
    const server = doc();
    server.hp = 4; // the GM changed HP too: the player's edit stays, it is sent next
    server.attributes.CON = 14; // a different attribute: taken in
    server.notes = 'from the GM';
    const next = adopt(draft, base, server);
    expect(next.hp).toBe(9);
    expect(next.attributes).toEqual({ STR: 16, CON: 14 });
    expect(next.notes).toBe('from the GM');
  });

  it('does not change its input', () => {
    const draft = doc();
    const server = doc();
    server.hp = 1;
    adopt(draft, doc(), server);
    expect(draft.hp).toBe(10);
  });
});

describe('issueFor', () => {
  it('finds the rule break that concerns a part of the sheet', () => {
    const issues = [{ key: 'encumbrance', message: 'x', status: 'pending' }];
    expect(issueFor(issues, 'encumbrance')?.status).toBe('pending');
    expect(issueFor(issues, 'hp')).toBeUndefined();
    expect(issueFor(undefined, 'hp')).toBeUndefined();
  });
});
