const imageData = /^data:image\/(?:png|jpeg|svg\+xml);base64,[A-Za-z0-9+/]+={0,2}$/

const storageKey = 'pldmgr.repository-icons.v1'
const catalogStorageKey = 'pldmgr.repository-catalog.v1'
const maxStoredLength = 2 * 1024 * 1024

function browserStorage() {
  try { return globalThis.localStorage } catch { return null }
}

function validatedIcons(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw Error('Invalid icon catalog')
  const icons = Object.create(null)
  for (const [name, image] of Object.entries(value)) {
    if (typeof image === 'string' && image.length <= 12288 && imageData.test(image)) icons[name] = image
  }
  return icons
}

export function createRepositoryIconCache(fetchIcons, storage = browserStorage()) {
  const cache = new Map()
  // Storage is optional (private browsing, quota limits, or older browsers).
  try {
    const saved = storage?.getItem(storageKey)
    if (saved && saved.length <= maxStoredLength) {
      for (const [key, entry] of JSON.parse(saved)) {
        if (typeof key === 'string' && typeof entry?.revision === 'string') {
          cache.set(key, { revision: entry.revision, icons: validatedIcons(entry.icons) })
        }
      }
    }
  } catch { cache.clear() }
  const persist = () => {
    try {
      const entries = [...cache]
      let saved = JSON.stringify(entries)
      while (saved.length > maxStoredLength && entries.length) {
        entries.shift()
        saved = JSON.stringify(entries)
      }
      storage?.setItem(storageKey, saved)
    } catch { /* In-memory caching still works when storage is unavailable. */ }
  }
  const tokens = new Map()
  let generation = 0
  const cachedData = data => ({ ...data, sources: data.sources.map(source => {
    const entry = cache.get(`${source.url}\n${source.id}`)
    return { ...source, payloads: source.payloads.map(payload => ({
      ...payload, icon: source.icons_revision ? entry?.icons[payload.icon_key] || null : null,
    })) }
  }) })
  return {
    restore() {
      try {
        const saved = storage?.getItem(catalogStorageKey)
        if (!saved || saved.length > maxStoredLength) return null
        const data = JSON.parse(saved)
        if (!Array.isArray(data?.sources) || !data.sources.every(source =>
          source && typeof source.url === 'string' && Array.isArray(source.payloads) &&
          source.payloads.every(payload => payload && typeof payload === 'object'))) return null
        return cachedData(data)
      } catch { return null }
    },
    remember(data) {
      try {
        if (!Array.isArray(data?.sources)) return
        // Keep the catalog separate so shared image data is stored only once.
        const saved = JSON.stringify(data, (key, value) => key === 'icon' ? undefined : value)
        if (saved.length <= maxStoredLength) storage?.setItem(catalogStorageKey, saved)
        else storage?.removeItem(catalogStorageKey)
      } catch { /* The current catalog remains usable without persistent storage. */ }
    },
    clear() {
      generation++; cache.clear(); tokens.clear()
      try { storage?.removeItem(storageKey); storage?.removeItem(catalogStorageKey) } catch { /* Storage may be disabled. */ }
    },
    async hydrate(data) {
      if (!Array.isArray(data?.sources)) return data
      const current = generation
      const sources = await Promise.all(data.sources.map(async source => {
        const key = `${source.url}\n${source.id}`
        let entry = cache.get(key)
        const token = {}
        tokens.set(key, token)
        if (source.icons_revision && entry?.revision !== source.icons_revision) {
          try {
            const response = await fetchIcons(`/repository_icons?source=${encodeURIComponent(source.id)}&revision=${encodeURIComponent(source.icons_revision)}`)
            if (!response.ok) throw Error('Icon catalog unavailable')
            const body = await response.json()
            entry = { revision: source.icons_revision, icons: validatedIcons(body.icons) }
            if (generation === current && tokens.get(key) === token) {
              cache.set(key, entry)
              persist()
            }
          } catch { /* Preserve the previous icons when a local fetch fails. */ }
        }
        return { ...source, payloads: (Array.isArray(source.payloads) ? source.payloads : []).map(payload => ({
          ...payload,
          icon: source.icons_revision ? entry?.icons[payload.icon_key] || null : null,
        })) }
      }))
      return { ...data, sources }
    },
  }
}
