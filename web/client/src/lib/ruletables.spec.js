import { describe, expect, it } from 'vitest';
import { textBlocks } from './ruletables.js';

describe('textBlocks', () => {
  it('groups consecutive bullet lines into one list and keeps paragraphs and tables in order', () => {
    const table = { title: 'Actions', rows: [] };
    const blocks = textBlocks('Intro line.\n* First.\n* Second [[link]].\n- Third.\n\nAfter.\n{{table: Actions}}\n✦Star item', [table]);
    expect(blocks).toEqual([
      { text: 'Intro line.' },
      { list: ['First.', 'Second [[link]].', 'Third.'] },
      { text: 'After.' },
      { table },
      { list: ['Star item'] },
    ]);
  });

  it('does not take a hyphen inside a sentence or a lone dash for a bullet', () => {
    expect(textBlocks('Half-elf text\n– Bore Tvartoff')).toEqual([{ text: 'Half-elf text' }, { text: '– Bore Tvartoff' }]);
  });

  it('puts an included card where its marker is, and drops a marker that has no card', () => {
    const card = { ref: 'core/ability/adaptive', name: 'Adaptive', body: 'Text.' };
    expect(textBlocks('Kin text.\n{{include: core/ability/adaptive}}\n{{include: nothing}}', [], [card])).toEqual([{ text: 'Kin text.' }, { card }]);
  });
});
