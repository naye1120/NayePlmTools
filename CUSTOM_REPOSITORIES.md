# Custom Payload Repositories

PS5 Payload Manager supports adding custom, third-party payload repositories (sources) in addition to the default official repository. This allows developers and communities to host and distribute their own payloads.

---

## JSON Format Specification

To create a repository, you must host a JSON file that conforms to the following schema.

### Core Rules:
1. Both formats are supported:
   - Object wrapper with top-level `"name"` and `"payloads"` array (recommended).
   - Bare array of payload objects `[ { ... }, { ... } ]`.
2. In the object wrapper, `"name"` is recommended for a readable catalog label. Field order does not matter; if the label is absent/empty, the source host is used.
3. The `"payloads"` field must be an array of payload objects.

### Example JSON:
```json
{
  "name": "My Custom Payloads",
  "icons_url": "icons.json",
  "payloads": [
    {
      "id": "ftpsrv",
      "name": "FTP Server",
      "filename": "ftpsrv_v0.19.elf",
      "url": "https://example.com/payloads/ftpsrv_v0.19.elf",
      "description": "A simple FTP server that accepts connections on port 2121",
      "version": "v0.19",
      "last_update": "2026-10-11",
      "category": "Networking",
      "checksum": "e6c1babbfd5e1b766d12b659853b514b9faedf6333cbe8cb514b1a3e79b7ce39",
      "author": "hippie68",
      "icon_key": "ftp-server"
    },
    {
      "id": "debug-tool",
      "name": "Debug Tool",
      "filename": "debug_tool.elf",
      "url": "https://example.com/payloads/debug_tool.elf",
      "description": "A helper utility to dump debug information.",
      "version": "v1.0"
    }
  ]
}
```

### Field Reference:

| Field | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| **`name`** (top-level) | String | Recommended | The display name of your repository catalog in the user interface (e.g. `"My Custom Payloads"`). |
| **`payloads`** (top-level) | Array | Yes | List of payload items available in this repository. |
| **`id`** (item-level) | String | Recommended | Stable identifier, unique within the repository, unchanged across versions (`[a-z0-9._-]`). Defaults to `slug(name)`. Stable IDs allow slots to maintain favorites and autoload entries across updates. |
| **`name`** (item-level) | String | Yes | Human-readable name of the specific payload. |
| **`filename`** (item-level) | String | Yes | The exact filename (including extension like `.elf`, `.bin`) when saved. |
| **`url`** (item-level) | String | Yes | Direct HTTP or HTTPS link where the payload binary can be downloaded. |
| **`version`** (item-level) | String | No | Version string used for update checks (e.g. `"v1.0"`). Follows version comparison rules described below. |
| **`last_update`** (item-level) | String | No | ISO 8601 date (e.g. `"2026-10-11"`). Serves as a tie-breaker when version strings compare equal. |
| **`description`** (item-level) | String | No | A description of what the payload does, displayed in the dashboard. |
| **`category`** (item-level) | String | No | The category name used to group payloads in the frontend (e.g. `"Networking"`). Defaults to `"Uncategorized"`. |
| **`checksum`** (item-level) | String | No | **SHA-256** hash: exactly 64 hexadecimal characters when nonempty. Missing/empty remains optional; malformed supplied values reject the entry. Every download attempt, including retries, is checked when a hash is supplied. If versions are identical but checksum differs, it is flagged as an update. |
| **`author`** (item-level) | String | No | Author or creator of the payload. |
| **`icons_url`** (top-level) | String | No | Relative or absolute HTTP/HTTPS URL of a separate icon catalog. Requires the object wrapper. |
| **`icon_key`** (item-level) | String | No | Key in the separate catalog’s `icons` map, up to 127 UTF-8 bytes. Multiple payloads can share one key. |
| **`details_url`** (item-level) | String | No | URL to documentation or README markdown for the payload. |
| **`release_notes`** (item-level) | String | No | Markdown release notes for this version. The backend field holds up to 2047 UTF-8 bytes, with UTF-8-safe truncation and an ellipsis when exceeded. This is a byte budget, not 2000 Unicode characters. |
| **`source`** (item-level) | String | No | Link to the upstream source repository or releases page. |

---

### Payload Icons

Declare `icons_url` on the object wrapper, such as `"icons_url": "icons.json"`.
Relative URLs resolve against the payload catalog URL; absolute HTTP/HTTPS URLs
are also supported. HTTPS catalogs cannot downgrade to HTTP for icons. Sources
without `icons_url`, including bare arrays, use the normal generated fallback.
Inline item-level `icon` data is not supported.

The separate file has this shape:

```json
{
  "icons": {
    "ftp-server": "data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHdpZHRoPScxMjgnIGhlaWdodD0nMTI4Jz48cmVjdCB3aWR0aD0nMTI4JyBoZWlnaHQ9JzEyOCcgcng9JzI0JyBmaWxsPScjM2I4MmY2Jy8+PHRleHQgeD0nNjQnIHk9JzgyJyBmb250LWZhbWlseT0nQXJpYWwsc2Fucy1zZXJpZicgZm9udC1zaXplPSc0NCcgZm9udC13ZWlnaHQ9J2JvbGQnIGZpbGw9JyNmZmYnIHRleHQtYW5jaG9yPSdtaWRkbGUnPkZUUDwvdGV4dD48L3N2Zz4="
  }
}
```

Values must be base64 PNG, JPEG or SVG data URIs, each at most 12,288 characters.
Direct image URLs and WebP are unsupported. Use square images around 128×128.
An icon catalog must be smaller than 4 MiB, with at most 4,096 unique keys;
keys are nonempty and at most 127 UTF-8 bytes. Malformed JSON, duplicate keys, unsupported data URIs or exceeded limits reject
that icon update while preserving the last valid cache and the payload catalog.
Images that fail to decode in the browser use the generated fallback.

The PS5 caches each source's icon catalog persistently. It checks for a refresh
when the source is listed and the cache is seven days old, or when a referenced
key is missing. Changed `icons_url` values trigger a refresh too. Failed updates
and still-missing keys are retried at most hourly unless a different missing-key
set is discovered. Catalog refreshes do not otherwise force icon refreshes.
When a catalog refresh fails, a valid cached payload list remains available with
a source error indicator. Cached icons are served locally to the browser, separately from payload metadata;
the browser reuses them while their content revision is unchanged.

**Settings → Repository Cache → Clear Cache** removes downloaded catalogs, icons
and their freshness records, then reloads them. Installed payloads, configured
sources, favorites, autoload and settings are preserved.

## Version Comparison Rules

Payload Manager implements a strict version comparison algorithm:

1. Whitespace is trimmed, and any leading `v` or `V` prefix is dropped (e.g. `v1.2` equals `1.2`).
2. The version string is tokenized into numeric runs (`0-9`) and alphabetical runs (`a-z`, `A-Z`). Separators like `.`, `-`, and `_` separate tokens.
3. Numeric tokens compare numerically without leading zeros (e.g. `1.11` > `1.2`, and `20261011` > `20260901`).
4. Alphabetical tokens compare case-insensitively. A numeric token takes precedence over an alphabetical token at the same position (`1.1` > `1.a`).
5. When one version string runs out of tokens first:
   - If the next token on the other version is alphabetical, that version is treated as a pre-release and is considered **lower** (e.g. `1.7` > `1.7beta3`, `1.0` > `1.0-alpha`).
   - If the next token is numeric, it is treated as a sub-version and is considered **higher** (e.g. `1.0.1` > `1.0`).
6. If the version strings compare equal, `last_update` is used as a tie-breaker (newer date takes precedence).

---

## Hosting Your Repository

1. **Accessibility**: Upload your JSON file and your payload binaries to any static hosting provider (e.g., GitHub Pages, Vercel, Netlify, or your own VPS). The PS5 must be able to resolve and reach the host IP.
2. **CORS Headers**: Catalog and binary downloads happen in the PS5 daemon and do not require browser CORS. The frontend fetches README/details content directly, so that content's host must allow cross-origin browser GETs when it is on another origin. For public documentation, for example:
   ```http
   Access-Control-Allow-Origin: *
   ```

## Integrity, Storage and Markdown

HTTP and HTTPS custom sources remain supported. HTTPS validates the certificate
and hostname and permits only HTTPS redirects; certificate failures do not retry
with verification disabled. A supplied hash checks file integrity, not the
identity or safety of its author: a manifest publisher controls both the binary
URL and its hash. Use sources you trust; mandatory hashes or LAN authentication
are separate policies that are not enabled here.

Catalog downloads are bounded at 16 MiB; payload downloads at 512 MiB.
Collections also have 4,096-item and 64 MiB allocation limits. Required string
fields must fit their backend buffers. A failed/over-limit serialized response
returns an error instead of a broken partial JSON document.

Managed installations use slots under `/data/pldmgr/payloads/repo/`;
uploads/USB imports use `/data/pldmgr/payloads/local/`. Payload and metadata
publication uses staging/recovery so a failed install does not silently replace
a valid previous version. Favorites/autoload use stable slot keys. The manager
hides its own payload in custom repositories and rejects self-installation from
them, uploads and USB. Only the official repository can offer `id: "pldmgr"`
(or `name: "pldmgr"` when the ID is omitted) as a self-update. Self-updates require
a SHA-256 checksum and a stable major.minor.patch ELF version matching the
manifest (the manifest may prefix the version with `v`). They are stored at
`/data/pldmgr/payloads/pldmgr/pldmgr_vX.Y.Z.elf`, outside slots, and apply at the
next startup. They cannot be launched through payload/autoload APIs.

README and release-note markdown preserves safe links, code, headings and tables
through DOM sanitization. Arbitrary images, remote media, styles and scripts are
excluded even if they use absolute URLs. Emoji use bundled local artwork.
Loaded content and browser-cached README responses can remain readable without
WAN; uncached remote documentation still requires a connection. Repository icons
are a separate data-URI feature, not an exception for raw markdown images.

---

## Adding the Source in Payload Manager

1. Open the Payload Manager dashboard.
2. Navigate to **Settings** (gear icon) and select **Manage Sources**.
3. Click **Add Source**, paste the direct HTTP/HTTPS URL to your `payloads.json` file, and press **Add**.
4. The dashboard will validate the source and add it to your catalog list.
