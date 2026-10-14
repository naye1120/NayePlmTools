export function formatSyncTime(unixSeconds, locale, useLocalFields = false) {
  const date = new Date(unixSeconds * 1000)
  if (!useLocalFields) return date.toLocaleString(locale)
  // Older console WebKit can format locale dates in UTC. Read local fields directly.
  const pad = value => String(value).padStart(2, '0')
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
}
