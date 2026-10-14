import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'

const html = await readFile(new URL('../frontend/dist/index.html', import.meta.url), 'utf8')
const png = await readFile(new URL('../frontend/src/assets/emoji/sprites.png', import.meta.url))
const embedded = 'data:image/png;base64,' + png.toString('base64')
assert.equal(html.split(embedded).length - 1, 1, 'emoji PNG must be embedded exactly once in the production HTML')
assert.ok(html.includes('name="emoji-attribution"'), 'emoji artwork attribution must survive the build')
assert.ok(!html.includes('https://cdn.jsdelivr.net/gh/jdecked/twemoji'), 'production emoji must not depend on a CDN')
console.log('Production bundle: single embedded emoji PNG, attribution and no Twemoji CDN dependency verified')
