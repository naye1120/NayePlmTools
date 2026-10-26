function githubReference(value) {
  if (typeof value !== 'string' || !value) return null;
  try {
    const url = new URL(value);
    if (!['http:', 'https:'].includes(url.protocol) || !/^(www\.)?github\.com$/i.test(url.hostname)) return null;
    const parts = url.pathname.split('/').filter(Boolean);
    if (parts.length < 2) return null;
    const repo = `${parts[0]}/${parts[1].replace(/\.git$/, '')}`;
    const tag = parts[2] === 'releases' && ['download', 'tag'].includes(parts[3]) && parts[4]
      ? decodeURIComponent(parts[4]) : null;
    return { repo, tag };
  } catch { return null; }
}

export function releaseCandidates(payload) {
  // The actual download identifies the upstream repo/tag even when source is
  // an old repository alias or the release tag differs from the display version.
  const refs = [payload?.source_direct, payload?.url, payload?.source, payload?.repository, payload?.repo_url]
    .map(githubReference).filter(Boolean);
  const reference = refs.find(ref => ref.tag) || refs[0];
  if (!reference) return [];
  const version = typeof payload.version === 'string' ? payload.version.trim() : '';
  const tags = [reference.tag, version, version ? (version.startsWith('v') ? version.slice(1) : `v${version}`) : ''];
  return [...new Set(tags.filter(Boolean))].map(tag => ({
    tag,
    url: `https://api.github.com/repos/${reference.repo}/releases/tags/${encodeURIComponent(tag)}`,
  }));
}

export async function fetchReleaseNotes(payload, options = {}, transport = fetch) {
  for (const { url, tag } of releaseCandidates(payload)) {
    const response = await transport(url, { ...options, headers: { Accept: 'application/vnd.github+json' } });
    if (response.status === 404) continue;
    if (!response.ok) throw Error('Release details unavailable');
    const release = await response.json();
    if (release.tag_name !== tag) continue;
    return typeof release.body === 'string' ? release.body : '';
  }
  return '';
}
