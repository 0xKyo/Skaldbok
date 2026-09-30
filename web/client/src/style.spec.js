import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';

// A comment left open or a brace left unclosed swallows the rules after it: everything below that point would only apply inside an
// @media block (so the page broke on a phone and looked fine on a wide screen). This keeps the stylesheet well formed.
const file = ['src/style.css', 'client/src/style.css', 'web/client/src/style.css'].map((p) => resolve(process.cwd(), p)).find((p) => existsSync(p));
const css = readFileSync(file, 'utf8');

describe('style.css', () => {
  it('closes every comment', () => {
    expect((css.match(/\/\*/g) ?? []).length).toBe((css.match(/\*\//g) ?? []).length);
  });

  it('balances its braces, and never closes one that is not open', () => {
    const code = css.replace(/\/\*[\s\S]*?\*\//g, '');
    let depth = 0;
    for (const ch of code) {
      if (ch === '{') depth += 1;
      else if (ch === '}') depth -= 1;
      expect(depth).toBeGreaterThanOrEqual(0);
    }
    expect(depth).toBe(0);
  });

  it('keeps the sheet rules outside any media query, so a phone gets them too', () => {
    const code = css.replace(/\/\*[\s\S]*?\*\//g, '');
    let depth = 0;
    const depths = [];
    for (let i = 0; i < code.length; i += 1) {
      if (code[i] === '{') depth += 1;
      else if (code[i] === '}') depth -= 1;
      if (code.startsWith('.gems {', i)) depths.push(depth);      // the attribute gems are drawn on every screen (and resized in some)
    }
    expect(depths).toContain(0);
  });
});
