import test from 'node:test';
import assert from 'node:assert/strict';
import React from 'react';
import { renderToString } from 'react-dom/server';
import ErrorBoundary from '../src/components/ui/ErrorBoundary.js';

// Mock window for SSR test environment
if (!global.window) {
  global.window = {};
}

test('favorite item resolution maps string keys in favoritePayloads to dashboard objects', () => {
  const dashboardItems = [
    { key: 'repo/etaHEN@a1b2c3d4', slot: 'repo/etaHEN@a1b2c3d4', filename: 'etaHEN.elf', name: 'etaHEN', kind: 'repo' },
    { key: 'local/payload1', slot: 'local/payload1', filename: 'payload1.elf', name: 'payload1', kind: 'local' },
    { key: 'repo/item2@11223344', slot: 'repo/item2@11223344', filename: 'item2.elf', name: 'item2', kind: 'repo' },
  ];

  const favoritePayloads = ['local/payload1', 'repo/etaHEN@a1b2c3d4', 'nonexistent/key'];

  const itemMap = new Map();
  for (const d of dashboardItems) {
    if (d && d.key) itemMap.set(d.key, d);
  }

  const activeFavoriteItems = favoritePayloads
    .map(k => itemMap.get(k))
    .filter(Boolean);

  assert.equal(activeFavoriteItems.length, 2);
  assert.equal(activeFavoriteItems[0].key, 'local/payload1');
  assert.equal(activeFavoriteItems[1].key, 'repo/etaHEN@a1b2c3d4');
  assert.equal(typeof activeFavoriteItems[0], 'object');
  assert.equal(typeof activeFavoriteItems[1], 'object');

  const activeFavoriteKeys = new Set(activeFavoriteItems.map(d => d.key));
  const unfavorited = dashboardItems.filter(d => !activeFavoriteKeys.has(d.key));

  assert.equal(unfavorited.length, 1);
  assert.equal(unfavorited[0].key, 'repo/item2@11223344');
});

test('favorite move and toggle logic preserves valid array without throwing', () => {
  let favs = ['a', 'b', 'c'];

  // Toggle existing removes it
  const removeB = favs.includes('b') ? favs.filter(p => p !== 'b') : [...favs, 'b'];
  assert.deepEqual(removeB, ['a', 'c']);

  // Move 'b' left
  const moveLeft = (arr, item) => {
    const idx = arr.indexOf(item);
    if (idx <= 0) return arr;
    const next = [...arr];
    next[idx] = next[idx - 1];
    next[idx - 1] = item;
    return next;
  };

  assert.deepEqual(moveLeft(favs, 'b'), ['b', 'a', 'c']);
  assert.deepEqual(moveLeft(favs, 'a'), ['a', 'b', 'c']); // already at start

  // Move right
  const moveRight = (arr, item) => {
    const idx = arr.indexOf(item);
    if (idx === -1 || idx >= arr.length - 1) return arr;
    const next = [...arr];
    next[idx] = next[idx + 1];
    next[idx + 1] = item;
    return next;
  };

  assert.deepEqual(moveRight(favs, 'b'), ['a', 'c', 'b']);
  assert.deepEqual(moveRight(favs, 'c'), ['a', 'b', 'c']); // already at end
});

test('legacy_paths migration safely updates favorites', () => {
  const legacyMap = {
    '/data/payloads/etaHEN.bin': 'repo/etaHEN@a1b2c3d4',
    'kstuff.elf': 'repo/kstuff@99887766',
  };

  const oldFavorites = ['/data/payloads/etaHEN.bin', 'kstuff.elf', 'unaffected.bin'];

  const mapped = oldFavorites.map((fav) => {
    if (legacyMap[fav]) return legacyMap[fav];
    const fn = fav.split('/').pop();
    if (legacyMap[fn]) return legacyMap[fn];
    return fav;
  });

  assert.deepEqual(mapped, ['repo/etaHEN@a1b2c3d4', 'repo/kstuff@99887766', 'unaffected.bin']);
});

test('makeCard item fallback resolver handles raw string keys gracefully', () => {
  const dashboardItems = [
    { key: 'repo/etaHEN@a1b2c3d4', slot: 'repo/etaHEN@a1b2c3d4', filename: 'etaHEN.elf', name: 'etaHEN', kind: 'repo' },
  ];
  const itemMap = new Map(dashboardItems.map(d => [d.key, d]));

  const resolveItem = (item) => {
    if (!item) return null;
    return (typeof item === 'string')
      ? (itemMap.get(item) || {
          key: item,
          slot: item,
          path: item,
          filename: item.split('/').pop(),
          name: item.split('/').pop(),
          kind: 'local',
          isUsb: item.startsWith('/mnt/usb'),
        })
      : item;
  };

  // When item is a string matching dashboardItem
  const resolved1 = resolveItem('repo/etaHEN@a1b2c3d4');
  assert.equal(resolved1.filename, 'etaHEN.elf');
  assert.equal(resolved1.key, 'repo/etaHEN@a1b2c3d4');

  // When item is an unknown string
  const resolved2 = resolveItem('unknown_payload.bin');
  assert.equal(resolved2.filename, 'unknown_payload.bin');
  assert.equal(resolved2.isUsb, false);

  // When item is a USB string
  const resolved3 = resolveItem('/mnt/usb0/loader.elf');
  assert.equal(resolved3.filename, 'loader.elf');
  assert.equal(resolved3.isUsb, true);
});

test('ErrorBoundary getDerivedStateFromError catches errors and renderToString displays error banner', () => {
  const derived = ErrorBoundary.getDerivedStateFromError(new Error('Fatal test crash'));
  assert.equal(derived.hasError, true);
  assert.equal(derived.error.message, 'Fatal test crash');

  // Verify ErrorBoundary state renders error UI
  const boundary = new ErrorBoundary({});
  boundary.state = {
    hasError: true,
    error: new Error('Render crash verification'),
    errorInfo: { componentStack: '\n at ThrowingComponent' },
  };

  const rendered = renderToString(boundary.render());
  assert.ok(rendered.includes('Runtime Error Detected'));
  assert.ok(rendered.includes('Render crash verification'));
  assert.ok(rendered.includes('ThrowingComponent'));
  assert.ok(rendered.includes('Reload Page'));
});

test('favorite directional arrows: canMoveLeft and canMoveRight are strictly false for unfavorited items', () => {
  const favoritePayloads = ['fav0', 'fav1', 'fav2'];

  const getMoveFlags = (key) => {
    const favIndex = favoritePayloads.indexOf(key);
    const isFavorite = favIndex !== -1;
    return {
      canMoveLeft: isFavorite && favIndex > 0,
      canMoveRight: isFavorite && favIndex < favoritePayloads.length - 1,
    };
  };

  // Unfavorited item: neither direction should be allowed
  assert.deepEqual(getMoveFlags('unfavorited_item'), { canMoveLeft: false, canMoveRight: false });

  // First item: can only move right
  assert.deepEqual(getMoveFlags('fav0'), { canMoveLeft: false, canMoveRight: true });

  // Middle item: can move left and right
  assert.deepEqual(getMoveFlags('fav1'), { canMoveLeft: true, canMoveRight: true });

  // Last item: can only move left
  assert.deepEqual(getMoveFlags('fav2'), { canMoveLeft: true, canMoveRight: false });
});

test('ErrorBoundary methods are constructor-bound and dismissable', () => {
  const boundary = new ErrorBoundary({ canDismiss: true });
  assert.equal(typeof boundary.handleReload, 'function');
  assert.equal(typeof boundary.handleReset, 'function');
  assert.equal(typeof boundary.handleDismiss, 'function');

  // Provide mock updater for unmounted instance
  boundary.state = { hasError: true, error: new Error('Dismiss test'), errorInfo: null };
  boundary.updater = {
    enqueueSetState: (inst, partial) => {
      inst.state = { ...inst.state, ...partial };
    },
  };
  boundary.handleDismiss();
  assert.equal(boundary.state.hasError, false);
  assert.equal(boundary.state.error, null);
});

test('backend FAVORITES parsing and formatting handles comma-separated strings', () => {
  const backendConfig = {
    FAVORITES: 'repo/etaHEN@a1b2c3d4,local/payload1,repo/item2@11223344',
    FAVORITES_INITIALIZED: true,
  };

  const parseFavorites = (data) => {
    if (typeof data?.FAVORITES === 'string') {
      return data.FAVORITES ? data.FAVORITES.split(',').filter(Boolean) : [];
    }
    return null;
  };

  const parsed = parseFavorites(backendConfig);
  assert.deepEqual(parsed, ['repo/etaHEN@a1b2c3d4', 'local/payload1', 'repo/item2@11223344']);

  // Empty string returns empty array
  assert.deepEqual(parseFavorites({ FAVORITES: '', FAVORITES_INITIALIZED: true }), []);

  // Format back to payload string
  const formatPayload = (favArray) => JSON.stringify({ FAVORITES: favArray.join(',') });
  assert.equal(
    formatPayload(['repo/etaHEN@a1b2c3d4', 'local/payload1']),
    '{"FAVORITES":"repo/etaHEN@a1b2c3d4,local/payload1"}'
  );
});

test('backend favorites sync prioritizes backend when initialized and migrates local when uninitialized', () => {
  const syncFavorites = (configData, localStored) => {
    if (configData?.FAVORITES_INITIALIZED === true && typeof configData?.FAVORITES === 'string') {
      return {
        active: configData.FAVORITES ? configData.FAVORITES.split(',').filter(Boolean) : [],
        shouldMigrate: false,
      };
    }
    if (configData?.FAVORITES_INITIALIZED === false && Array.isArray(localStored) && localStored.length > 0) {
      return {
        active: localStored,
        shouldMigrate: true,
      };
    }
    return {
      active: [],
      shouldMigrate: false,
    };
  };

  // Initialized backend with items -> use backend items
  const res1 = syncFavorites({ FAVORITES: 'repo/1,repo/2', FAVORITES_INITIALIZED: true }, ['local/old']);
  assert.deepEqual(res1.active, ['repo/1', 'repo/2']);
  assert.equal(res1.shouldMigrate, false);

  // Initialized backend with empty list (user removed all) -> use empty, do not migrate stale local
  const res2 = syncFavorites({ FAVORITES: '', FAVORITES_INITIALIZED: true }, ['local/old']);
  assert.deepEqual(res2.active, []);
  assert.equal(res2.shouldMigrate, false);

  // Uninitialized backend (first run) -> migrates local favorites
  const res3 = syncFavorites({ FAVORITES: '', FAVORITES_INITIALIZED: false }, ['local/old']);
  assert.deepEqual(res3.active, ['local/old']);
  assert.equal(res3.shouldMigrate, true);
});


