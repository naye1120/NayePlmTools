/* eslint-disable react-refresh/only-export-components */
import React, { createContext, useContext, useState, useEffect, useCallback, useMemo, useRef } from 'react';
import { slug } from '../utils/versions';
import { createRepositoryIconCache } from '../utils/repositoryIcons';
import { normalizeUrl } from '../utils/payloadStatus';

import { apiFetch, mutationFetch } from '../utils/mutationRequest';

const RepositoryContext = createContext();

export const useRepository = () => useContext(RepositoryContext);

export const RepositoryProvider = ({ children }) => {
  const [repoData, setRepoData] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(false);

  const [iconCache] = useState(() => createRepositoryIconCache(apiFetch));
  const requestGeneration = useRef(0);

  const fetchRemote = useCallback(async (force = false) => {
    const generation = ++requestGeneration.current;
    setLoading(true);
    setError(false);
    try {
      const endpoint = force ? '/repository_refresh' : '/repository_payloads';
      const res = await (force ? mutationFetch(endpoint) : apiFetch(endpoint));
      if (!res.ok) throw new Error();
      const data = await res.json();
      if (generation !== requestGeneration.current) return;

      const hydrated = await iconCache.hydrate(data);
      if (generation === requestGeneration.current) setRepoData(hydrated);
    } catch (err) {
      console.error(err);
      if (generation === requestGeneration.current) {
        setError(true);
        setRepoData({ sources: [] });
      }
    } finally {
      if (generation === requestGeneration.current) setLoading(false);
    }
  }, [iconCache]);

  const clearCache = useCallback(async () => {
    const response = await mutationFetch('/repository_cache_clear');
    if (!response.ok) throw Error('Repository cache could not be cleared');
    requestGeneration.current++;
    iconCache.clear();
    setRepoData({ sources: [] });
    await fetchRemote();
  }, [fetchRemote, iconCache]);

  // Fetch once on mount
  useEffect(() => {
    if (repoData === null) {
      // eslint-disable-next-line react-hooks/set-state-in-effect
      fetchRemote();
    }
  }, [fetchRemote, repoData]);

  // Normalized sources list
  const sources = useMemo(() => {
    if (!repoData) return [];
    if (Array.isArray(repoData.sources)) {
      return repoData.sources.map((src) => ({
        id: src.id || src.url,
        name: src.name || 'Repository',
        url: src.url || '',
        last_update: src.last_update || 0,
        error: src.error || null,
        payloads: Array.isArray(src.payloads)
          ? src.payloads.map((p) => ({
              ...p,
              id: p.id || slug(p.name || p.filename),
            }))
          : [],
      }));
    }
    // Backward compatibility with bare array or single payloads array
    const rawList = Array.isArray(repoData)
      ? repoData
      : Array.isArray(repoData.payloads)
      ? repoData.payloads
      : repoData.payloads && Array.isArray(repoData.payloads.payloads)
      ? repoData.payloads.payloads
      : [];

    return [
      {
        id: 'default',
        name: repoData.name || 'Default Repository',
        url: repoData.repo_url || '',
        last_update: repoData.last_update || 0,
        error: null,
        payloads: rawList.map((p) => ({
          ...p,
          id: p.id || slug(p.name || p.filename),
        })),
      },
    ];
  }, [repoData]);

  // Index by source_url -> id -> entry
  const repoIndex = useMemo(() => {
    const map = new Map(); // normalized_url -> Map(id -> entry)

    for (const src of sources) {
      const nUrl = normalizeUrl(src.url);
      let srcMap = map.get(nUrl);
      if (!srcMap) {
        srcMap = new Map();
        map.set(nUrl, srcMap);
      }
      for (const p of src.payloads) {
        if (p.id) {
          srcMap.set(p.id, { ...p, source_url: src.url, source_name: src.name, source_id: src.id });
        }
      }
    }
    return map;
  }, [sources]);

  // Helper lookup functions
  const getRepoEntry = useCallback(
    (sourceUrl, id) => {
      if (!id) return null;
      const nUrl = normalizeUrl(sourceUrl);
      const srcMap = repoIndex.get(nUrl);
      return srcMap ? srcMap.get(id) || null : null;
    },
    [repoIndex]
  );

  const findRepoEntryById = useCallback(
    (id, preferSrcUrl = null) => {
      if (!id) return null;
      if (preferSrcUrl) {
        const match = getRepoEntry(preferSrcUrl, id);
        if (match) return match;
      }
      let found = null;
      repoIndex.forEach((srcMap) => {
        if (!found && srcMap.has(id)) {
          found = srcMap.get(id);
        }
      });
      return found;
    },
    [getRepoEntry, repoIndex]
  );

  const findRepoEntryByFilename = useCallback(
    (filename, preferSrcUrl = null) => {
      if (!filename) return null;
      const lower = filename.toLowerCase();

      // Check preferred source first if specified
      if (preferSrcUrl) {
        const srcMap = repoIndex.get(normalizeUrl(preferSrcUrl));
        if (srcMap) {
          let preferredMatch = null;
          srcMap.forEach((entry) => {
            if (!preferredMatch && (entry.filename === filename || entry.filename?.toLowerCase() === lower)) {
              preferredMatch = entry;
            }
          });
          if (preferredMatch) return preferredMatch;
        }
      }

      let found = null;
      repoIndex.forEach((srcMap) => {
        if (!found) {
          srcMap.forEach((entry) => {
            if (!found && (entry.filename === filename || entry.filename?.toLowerCase() === lower)) {
              found = entry;
            }
          });
        }
      });
      return found;
    },
    [repoIndex]
  );

  return (
    <RepositoryContext.Provider
      value={{
        repoData,
        sources,
        loading,
        error,
        fetchRemote,
        clearCache,
        repoIndex,
        getRepoEntry,
        findRepoEntryById,
        findRepoEntryByFilename,
      }}
    >
      {children}
    </RepositoryContext.Provider>
  );
};
