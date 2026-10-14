import test from 'node:test';
import assert from 'node:assert/strict';
import { formatVersionTag, formatSourceTag, parsePayloadName, getBaseName, cleanDisplayName, isSystemPayload } from '../src/utils/helpers.js';

test('formatVersionTag preserves short and normal versions', () => {
  assert.equal(formatVersionTag('0.1'), '0.1');
  assert.equal(formatVersionTag('v0.4.4'), 'v0.4.4');
  assert.equal(formatVersionTag('v0.17.2'), 'v0.17.2');
  assert.equal(formatVersionTag('2.5B'), '2.5B');
  assert.equal(formatVersionTag('12345678'), '12345678');
});

test('formatVersionTag truncates long versions displaying ending part with ellipsis', () => {
  // Length > 8: shows ... followed by the end of the string
  assert.equal(formatVersionTag('123456789'), '...456789');
  assert.equal(formatVersionTag('v1.2.3.4567'), '...3.4567');
  assert.equal(formatVersionTag('v1.0.0-beta.2'), '...beta.2');
  assert.equal(formatVersionTag('7f3a9b1c2e4'), '...b1c2e4');
});

test('formatVersionTag supports custom maxLen parameter', () => {
  assert.equal(formatVersionTag('123456789', 6), '...6789');
  assert.equal(formatVersionTag('12345', 6), '12345');
});

test('formatVersionTag handles empty, null, and non-string inputs safely', () => {
  assert.equal(formatVersionTag(null), '');
  assert.equal(formatVersionTag(undefined), '');
  assert.equal(formatVersionTag(''), '');
  assert.equal(formatVersionTag(123), '');
});

test('formatSourceTag formats and truncates long source names with trailing ellipsis', () => {
  assert.equal(formatSourceTag('Homebrew Repo'), 'Homebrew Repo');
  assert.equal(formatSourceTag('My Source Of Payloads 2026'), 'My Source Of Payl...');
  assert.equal(formatSourceTag('Short', 10), 'Short');
  assert.equal(formatSourceTag('A Very Long Custom Name', 12), 'A Very Lo...');
  assert.equal(formatSourceTag(null), '');
  assert.equal(formatSourceTag(''), '');
});

test('getBaseName extracts clean normalized lowercase base names', () => {
  assert.equal(getBaseName('Elfldr.elf'), 'elfldr');
  assert.equal(getBaseName('elfldr.elf'), 'elfldr');
  assert.equal(getBaseName('elfldr_v0.26.elf'), 'elfldr');
  assert.equal(getBaseName('Klogsrv_V0.9.elf'), 'klogsrv');
  assert.equal(getBaseName('Klogsrv V0.9.elf'), 'klogsrv');
  assert.equal(getBaseName('klogsrv_v0.9.elf'), 'klogsrv');
  assert.equal(getBaseName('/data/pldmgr/zftpd_v1.5.0.elf'), 'zftpd');
  assert.equal(getBaseName('ps5debug-NG_1.3.0.elf'), 'ps5debug-ng');
  assert.equal(getBaseName('etaHEN-ps5.bin'), 'etahen');
  assert.equal(getBaseName(null), '');
  assert.equal(getBaseName(undefined), '');
  assert.equal(getBaseName(''), '');
});

test('parsePayloadName parses names and versions correctly including space-separated versions', () => {
  const r1 = parsePayloadName('/data/payloads/Klogsrv V0.9.elf');
  assert.equal(r1.displayName, 'Klogsrv');
  assert.equal(r1.version, 'V0.9');

  const r2 = parsePayloadName('/data/payloads/klogsrv_v0.9.elf');
  assert.equal(r2.displayName, 'klogsrv');
  assert.equal(r2.version, 'v0.9');

  const r3 = parsePayloadName('/data/payloads/Elfldr.elf');
  assert.equal(r3.displayName, 'Elfldr');
  assert.equal(r3.version, null);

  const r4 = parsePayloadName('!2500');
  assert.equal(r4.displayName, 'Delay (2.5s)');
  assert.equal(r4.isDelay, true);
  assert.equal(r4.delaySeconds, 2.5);
});

test('cleanDisplayName removes trailing duplicate versions matching version tag', () => {
  assert.equal(cleanDisplayName('Klogsrv V0.9', 'V0.9'), 'Klogsrv');
  assert.equal(cleanDisplayName('Klogsrv V0.9', 'v0.9'), 'Klogsrv');
  assert.equal(cleanDisplayName('Klogsrv 0.9', 'v0.9'), 'Klogsrv');
  assert.equal(cleanDisplayName('Klogsrv_v0.9', '0.9'), 'Klogsrv');
  assert.equal(cleanDisplayName('klogsrv-v0.9', 'v0.9'), 'klogsrv');
  assert.equal(cleanDisplayName('ps5debug-NG 1.3.0', '1.3.0'), 'ps5debug-NG');
  assert.equal(cleanDisplayName('klogsrv', 'v0.9'), 'klogsrv');
  assert.equal(cleanDisplayName('klogsrv', null), 'klogsrv');
  assert.equal(cleanDisplayName(null, 'v0.9'), '');
  assert.equal(cleanDisplayName(undefined, 'v0.9'), '');
});

test('isSystemPayload correctly identifies pldmgr and payload-manager binaries and variations', () => {
  assert.equal(isSystemPayload('pldmgr.elf'), true);
  assert.equal(isSystemPayload('pldmgr_v0.5.1.elf'), true);
  assert.equal(isSystemPayload('/data/pldmgr/payloads/repo/pldmgr@abcd1234/pldmgr_v0.6.0.elf'), true);
  assert.equal(isSystemPayload('payload-manager.elf'), true);
  assert.equal(isSystemPayload('payload-manager_v1.0.bin'), true);
  assert.equal(isSystemPayload('payload_manager.elf'), true);
  assert.equal(isSystemPayload('payload manager.elf'), true);
  assert.equal(isSystemPayload('payloadmanager.bin'), true);
  assert.equal(isSystemPayload('Payload Manager'), true);
  assert.equal(isSystemPayload('pldmgr'), true);
  assert.equal(isSystemPayload('etaHEN_2.5B.bin'), false);
  assert.equal(isSystemPayload('kstuff_v1.6.7.elf'), false);
  assert.equal(isSystemPayload(null), false);
  assert.equal(isSystemPayload(''), false);
});

test('pldmgr payloads are correctly filtered from repository payload lists', () => {
  const isPldmgrItem = (p) => {
    const idLower = (p.id || '').toLowerCase();
    const nameLower = (p.name || '').toLowerCase();
    return (
      isSystemPayload(p.filename) ||
      isSystemPayload(p.name) ||
      isSystemPayload(p.id) ||
      idLower === 'pldmgr' ||
      idLower === 'payload-manager' ||
      idLower === 'payload_manager' ||
      nameLower === 'pldmgr' ||
      nameLower === 'payload manager' ||
      nameLower === 'payload-manager' ||
      nameLower === 'payload_manager'
    );
  };

  const mockPayloads = [
    { id: 'pldmgr', name: 'Payload Manager', filename: 'pldmgr.elf' },
    { id: 'pldmgr', name: 'pldmgr', filename: 'pldmgr_v0.6.0.elf' },
    { id: 'custom-pldmgr', name: 'My pldmgr', filename: 'pldmgr.elf' },
    { id: 'custom-pldmgr-name', name: 'Payload Manager', filename: 'custom.elf' },
    { id: 'payload_manager', name: 'payload manager', filename: 'payload_manager.elf' },
    { id: 'kstuff', name: 'Kstuff', filename: 'kstuff.elf' },
    { id: 'etahen', name: 'etaHEN', filename: 'etahen.bin' },
  ];

  const filtered = mockPayloads.filter((p) => !isPldmgrItem(p));
  assert.equal(filtered.length, 2);
  assert.equal(filtered[0].id, 'kstuff');
  assert.equal(filtered[1].id, 'etahen');
});



test('repository visibility admits only the official self-update and retains system payload guards', async () => {
  const { isRepositoryPayloadVisible, OFFICIAL_REPOSITORY_URL } = await import('../src/utils/payloadStatus.js');
  const payload = { id: 'pldmgr', name: 'pldmgr', filename: 'pldmgr_v0.6.0.elf' };
  assert.equal(isRepositoryPayloadVisible(payload, OFFICIAL_REPOSITORY_URL), true);
  for (const url of ['https://custom.invalid', `${OFFICIAL_REPOSITORY_URL}?custom=1`, `${OFFICIAL_REPOSITORY_URL}.evil`]) {
    assert.equal(isRepositoryPayloadVisible(payload, url), false);
  }
  assert.equal(isRepositoryPayloadVisible({ id: 'tool', filename: 'payload-manager.elf' }, OFFICIAL_REPOSITORY_URL), false);
  assert.equal(isSystemPayload(payload.filename), true);
});
