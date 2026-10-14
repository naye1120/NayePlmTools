const imageData = /^data:image\/(?:png|jpeg|svg\+xml);base64,[A-Za-z0-9+/]+={0,2}$/

export function createRepositoryIconCache(fetchIcons) {
  const cache = new Map()
  const tokens = new Map()
  let generation = 0
  return {
    clear() { generation++; cache.clear(); tokens.clear() },
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
            if (!body.icons || typeof body.icons !== 'object' || Array.isArray(body.icons)) throw Error('Invalid icon catalog')
            const icons = Object.create(null)
            for (const [name, value] of Object.entries(body.icons)) {
              if (typeof value === 'string' && value.length <= 12288 && imageData.test(value)) icons[name] = value
            }
            entry = { revision: source.icons_revision, icons }
            if (generation === current && tokens.get(key) === token) cache.set(key, entry)
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
