import test from 'node:test';
import assert from 'node:assert/strict';
import { createMarkdownParser, emojify, parseMarkdown } from '../src/utils/markdown.js';

test('native emoji expansion preserves combinations and unknown shortcodes', () => {
  assert.equal(emojify('Payload :pig: :rocket: 👩🏽‍💻 🇵🇱 1️⃣ :unknown:'), 'Payload 🐷 🚀 👩🏽‍💻 🇵🇱 1️⃣ :unknown:');
  assert.equal(emojify(null), '');
});

test('without a DOM, markdown stays readable escaped text and generates no assets', () => {
  const output = parseMarkdown('# Title :pig:\n<script>alert(1)</script><img src="https://evil.test/a">');
  assert.ok(output.startsWith('<pre># Title 🐷'));
  assert.ok(output.includes('&lt;script&gt;'));
  assert.ok(!output.includes('<script>'));
  assert.ok(!output.includes('<img'));
  assert.ok(!output.includes('cdn.jsdelivr.net'));
});

test('unavailable and unsupported sanitizers fail closed', () => {
  for (const purifier of [null, {}, { isSupported: true }, { isSupported: false, sanitize() { throw Error('must not run'); } }]) {
    assert.equal(createMarkdownParser(purifier)('<a href="javascript:evil()">&</a>'), '<pre>&lt;a href=&quot;javascript:evil()&quot;&gt;&amp;&lt;/a&gt;</pre>');
  }
});

test('markdown handles invalid input safely', () => {
  for (const value of [null, undefined, '', 123]) assert.equal(parseMarkdown(value), '');
});

test('polyfills provide standard Safari 11/12 runtime APIs', () => {
  assert.equal(typeof globalThis, 'object');
  assert.equal(['a', 'b', 'c'].at(-1), 'c');
  assert.equal('hello'.at(-1), 'o');
  assert.deepEqual([1, [2, [3, [4]]]].flat(Infinity), [1, 2, 3, 4]);
  assert.deepEqual([1, 2].flatMap((x) => [x, x * 2]), [1, 2, 2, 4]);
  assert.deepEqual(Object.fromEntries([['k', 'v']]), { k: 'v' });
  assert.equal(Object.hasOwn({ foo: 1 }, 'foo'), true);
  assert.equal('foo bar foo'.replaceAll('foo', 'baz'), 'baz bar baz');
});

test('bundled sprite matcher covers every artwork and shortcode without splitting combinations', async () => {
  const { default: identifiers } = await import('../src/assets/emoji/sprites.js');
  const { default: emojiMap } = await import('../src/utils/emojiMap.js');
  const { emojiSegments } = await import('../src/utils/emojiSprites.js');
  assert.equal(identifiers.length, 3838);
  for (const identifier of identifiers) {
    const text = identifier.split('-').map(hex => String.fromCodePoint(parseInt(hex, 16))).join('');
    const segments = emojiSegments(text);
    assert.equal(segments.length, 1, identifier);
    assert.equal(typeof segments[0].sprite, 'number', identifier);
    assert.equal(segments[0].text, text, identifier);
  }
  for (const [name, text] of Object.entries(emojiMap)) {
    assert.ok(emojiSegments(text).every(segment => typeof segment.sprite === 'number'), name);
  }
  for (const text of ['👩🏽‍💻', '🇵🇱', '1️⃣', '❤️', '🏴\u{e0067}\u{e0062}\u{e0065}\u{e006e}\u{e0067}\u{e007f}']) {
    assert.equal(emojiSegments(text).length, 1, text);
    assert.equal(emojiSegments(text)[0].text, text);
  }
  assert.deepEqual(emojiSegments('plain 123 # text :unknown:'), [{text: 'plain 123 # text :unknown:'}]);
});

test('emoji atlas is bounded and generated CSS can only load the bundled PNG', async () => {
  const { readFile } = await import('node:fs/promises');
  const png = await readFile(new URL('../src/assets/emoji/sprites.png', import.meta.url));
  assert.equal(png.readUInt32BE(16), 1280);
  assert.equal(png.readUInt32BE(20), 1200);
  assert.ok(png.length < 600000);
  const css = await readFile(new URL('../src/assets/emoji/sprites.css', import.meta.url), 'utf8');
  assert.equal([...css.matchAll(/url\(/g)].length, 1);
  assert.ok(css.includes('url("./sprites.png")'));
  assert.ok(!css.includes('https:') && !css.includes('http:'));
  assert.equal([...css.matchAll(/\.emoji-\d+ /g)].length, 0);
  assert.ok(css.length < 1000, 'position rules must be generated lazily, not bundled');
});
