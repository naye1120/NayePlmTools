import test from 'node:test';
import assert from 'node:assert/strict';
import { releaseCandidates, fetchReleaseNotes } from '../src/utils/releaseNotes.js';

test('release lookup follows the catalog artifact tag and repository, including encoded tags', () => {
  const candidates = releaseCandidates({ version: '1.7beta5-fix1', source: 'https://github.com/old/alias/releases',
    source_direct: 'https://github.com/owner/payload/releases/download/release%2F1.7beta5-fix1/payload.elf' });
  assert.deepEqual(candidates.map(candidate => candidate.tag), ['release/1.7beta5-fix1', '1.7beta5-fix1', 'v1.7beta5-fix1']);
  assert.equal(candidates[0].url, 'https://api.github.com/repos/owner/payload/releases/tags/release%2F1.7beta5-fix1');
  assert.ok(candidates.every(candidate => !candidate.url.includes('/latest')));
});

test('version-only lookup handles optional v prefixes without choosing the newest release', async () => {
  const requests = [];
  const notes = await fetchReleaseNotes({ version: '1.2', source: 'https://github.com/owner/repo/releases' }, {}, async url => {
    requests.push(url);
    return url.endsWith('/1.2') ? { status: 404 } : { ok: true, json: async () => ({ tag_name: 'v1.2', body: '**Catalog version**' }) };
  });
  assert.equal(notes, '**Catalog version**');
  assert.deepEqual(requests.map(url => url.split('/').pop()), ['1.2', 'v1.2']);
});

test('unsupported sources or missing version/tag do not make arbitrary release requests', () => {
  for (const payload of [null, {}, { version: '1', source: 'https://git.example.com/owner/repo' },
    { version: '1', source: 'https://github.com.evil.test/owner/repo' }, { source: 'https://github.com/owner/repo' }]) {
    assert.deepEqual(releaseCandidates(payload), []);
  }
});

test('release failures, empty notes, cancellation and mismatched tags stay scoped to the selected release', async () => {
  const payload = { release_notes: 'Stale mirror notes', version: 'v1', source: 'https://github.com/owner/repo' };
  await assert.rejects(fetchReleaseNotes(payload, {}, async () => ({ status: 403 })), /unavailable/);
  assert.equal(await fetchReleaseNotes(payload, {}, async () => ({ status: 404 })), '');
  assert.equal(await fetchReleaseNotes(payload, {}, async () => ({ ok: true, json: async () => ({ tag_name: 'v9', body: 'wrong version' }) })), '');
  assert.equal(await fetchReleaseNotes(payload, {}, async () => ({ ok: true, json: async () => ({ tag_name: 'v1', body: null }) })), '');
  const controller = new AbortController(); controller.abort();
  await assert.rejects(fetchReleaseNotes(payload, { signal: controller.signal }, async (_, options) => {
    assert.equal(options.signal, controller.signal);
    options.signal.throwIfAborted();
  }), { name: 'AbortError' });
});
