> ⚠️ **接手必读**：功能定稿后，本仓库**唯一事实来源**是同目录的 [`HANDOVER.md`](./HANDOVER.md) —— 任何改动前先读它。

# Development

This document describes how to build and deploy the **Payload Manager**.

## How to Build

### 1. Build the Frontend
You must build the React UI first. This converts the JSX into the `dist/index.html` file that gets embedded.
```bash
make frontend-build
```

### 2. Build the SDK Docker Image
If you haven't already, build the SDK environment:
```bash
docker build -t ps5-payload-sdk-pldmgr -f Dockerfile.sdk .
```

### 3. Build the ELF
Use the SDK container after building the frontend. Preserve your working ELF
and build a candidate at a new path; this regenerates changed embedded assets
without deleting previous candidates.
```bash
mkdir -p scratch
docker run --rm --network=none -v "$(pwd):/src" -w /src \
  ps5-payload-sdk-pldmgr make ELF=scratch/pldmgr-candidate.elf
```

The resulting candidate is `scratch/pldmgr-candidate.elf`. The SDK image and
embedded CA header must already exist for this offline compilation. The ELF
target does not track ordinary header dependencies; use a fresh candidate name
when changing headers alone. A normal release build with `make all` produces
`pldmgr.elf` at the repository root — **in this fork the default target instead
produces `NayePlmTools_<version>.elf`** (version comes from `MENU_VERSION` in
`include/pldmgr.h`).

## Automated Deploy

For a build-and-send cycle, `deploy.sh` builds the frontend, runs
`make clean all` in the existing `ps5-payload-sdk-pldmgr` image, and sends the
root `pldmgr.elf` to port 9021 using `socat`. It does **not** call `/shutdown`,
build the SDK image, or verify successful startup on the console. If replacing a
running instance, stop it explicitly with an authorized POST first; keep a
working ELF outside the root output path because the script cleans that output.
That clean also removes the embedded CA header, whose regeneration downloads the
CA bundle; use the candidate build above with an existing header for offline
compilation.

```bash
./deploy.sh [PS5_IP]
```
(Requires PS5 IP as the first argument).

## Manual Deploy
1. Load the built ELF using your autoloader, or send it to a separate running
   ELF loader on port 9021. Payload Manager does not provide that loader itself.
   The fork's ELF is `NayePlmTools_<version>.elf`.
2. The menu will be available at `http://[PS5_IP]:8911`.

## Automated Verification

The HTTP response suite compiles the actual `src/http_server.c` with stubbed
PS5 services and real libmicrohttpd response objects. It injects response-buffer,
response-creation, CORS-header, queue, reallocation, and file I/O failures, then
sends 10,000 requests through a real loopback HTTP server. It also checks
disconnected/chunked requests, concurrent uploads, and server-stop cleanup in
thread-per-connection mode. ASan (including leak detection) and UBSan
are enabled by default. Test files, storage paths, and binaries are temporary;
the suite does not connect to a console or launch payloads.

On a host with Clang, its sanitizer runtime, Python 3, pkg-config, and
libmicrohttpd/libcurl development files installed:

```bash
make test-host
make test-frontend
```

The container option builds libmicrohttpd **1.0.1**, matching `build_deps.sh`:

```bash
docker build -f tests/Dockerfile -t ps5-payload-manager-tests .
docker run --rm --network=none -v "$(pwd):/src:ro" ps5-payload-manager-tests make test-host
```

The build downloads test dependencies; the test container itself has external
networking disabled. Linux sanitizer support is included in the image. The
optional `--no-sanitize` argument is for hosts without sanitizer support; use
the default sanitizer run for verification.

The JSON encoder suite compiles actual `src/utils.c` and checks its output with
Python's independent JSON and UTF-8 decoders. It covers Unicode, all non-NUL
control bytes, invalid UTF-8 replacement, every small escape/scalar boundary,
truncation status, and guarded destination bounds, with ASan/UBSan enabled.
C strings cannot represent embedded NUL. The decoder rejects decoded NUL in the
requested value, while accepting valid escaped NUL in unrelated fields.

The decoder suite passes exact, non-NUL-terminated buffers to actual
`src/json_helpers.c`; it checks bounds, direct-member selection, all escapes,
UTF-8, surrogate pairs, malformed input, and strict/prefix overflow contracts.
Objects are validated completely with a nesting limit of 64. Strict extraction
clears the result on overflow; descriptions/release notes explicitly opt into
UTF-8-safe prefixes. The backend suite uses real repository, source save/load,
and slot metadata read/write code inside temporary storage. It parses their
results independently with Python. Unused network/PS5 code is discarded at
link time, and no download or payload launch occurs. `make test-host` runs all
host helper suites and actual-HTTP scenarios listed in the Makefile, with sanitizers.

The serializer suite exercises actual history/log/payload/process/repository
output at small and large capacities, sixfold escape expansion, failed format
calls, and metadata/source preservation on serialization failure. Repository
fixtures use fresh temporary caches and a recording download function that
aborts if called. Process records use a host fixture; this checks serialization,
not the PS5 kernel ABI. Current native builds pass without warnings.

The allocation suite executes actual repository parsing, managed-slot discovery,
cached-source serialization, and all USB discovery branches. It faults every
observed growth/read/path-copy allocation in its current fixtures and verifies
that failure clears results and releases partial collections. USB paths are
redirected to temporary fixtures using `PLDMGR_USB_ROOT_FORMAT`; production
defaults remain `/mnt/usb%d`. No fixture payload is launched.

The storage suite executes real uploads, USB imports, and repository installations
with local recorded download bytes. It checks conflicts, concurrent keys, complete
payload/metadata publication, I/O faults, media changes, and process-interruption
recovery. Storage uses unique staging directories and a journal/backup; startup
recovers interrupted commits before migration or serving requests. Public slot
layout is preserved. File syncing is checked; unsupported directory syncing limits
power-loss durability. Configuration remaps and migration are tested separately.

The integrity suite uses local HTTP/HTTPS servers and generated trusted,
untrusted, expired, and wrong-host certificates. It tests protocol restrictions,
body/size/I/O failures, strict optional checksum policy, refreshed manifest entry
validation, and preservation of installed data on failed retries. The actual
launcher runs against a recorded socket, covering canonical roots, regular files,
self guards, and symlink substitutions during descriptor-based opening. No real
loader is contacted.

API actions now require POST and a matching browser Origin/Referer, or an explicit
`X-PLDMGR-Request: 1` header when both are absent. Foreign origins cannot use that
marker to bypass checks. GET remains available for reads; anonymous/foreign reads
do not activate browser-mode autoload. API CORS/preflights are restricted to the
matching Host. This is browser CSRF protection, not LAN authentication or DNS
rebinding protection.

The frontend uses shared marked request helpers and same-origin unload beacons
with keepalive fallback. The Vite proxy preserves the frontend Host, and the mock
server mirrors action policy. Public mock README/JSON fixtures retain ordinary
cross-origin GET compatibility. The verification workflow also runs actual Chrome
against a native stub server and Vite, checking hostile requests, real pagehide
beacons, and zero rejected-request effects. Run `node tests/run_browser_mutation_tests.mjs` for these checks.

Settings now accept validated partial updates. Saves atomically replace individual
files, preserve unspecified fields/timestamps, and return errors on failed writes.
The UI sends only the fields it edits. Settings/favorites/autoload remaps and
source edits serialize read-modify-write; history reads serialize a snapshot of
the 20 most recent entries. The new `run_config_tests.py` covers actual persistence,
injected I/O failures and concurrent history/settings/source operations.

Run `python3 tests/run_config_tests.py --thread-sanitize` on a host supporting
Clang ThreadSanitizer; local macOS verification passed and CI has a macOS job.
A multi-file settings request validates fully first, publishes lists before scalar
settings, and reports 500 if any publication fails. Earlier completed files stay
published; per-file atomicity does not provide an all-or-nothing transaction.

Autoload now owns one joinable worker and returns a coherent locked snapshot.
Active reset preserves waiting/countdown/execution; terminal reset cannot rerun
it. Abort/stop interrupt delays and loader waits. Sequences are parsed once from
one regular file (4095 bytes, entries up to 511 bytes, delays up to one hour);
status retains that running sequence if the file is edited. Processed counts and
per-entry outcomes distinguish success/failure/skip. Anonymous/foreign status
reads remain passive; an authorized read explicitly signals frontend readiness.

Loader connects time out after five seconds, and full transfers after thirty;
nonblocking/poll loops handle partial sends and cooperative cancellation. A
cancel cannot undo a payload already executed or bytes already sent. The actual
worker, socketpair loader recorder and real HTTP worker fixtures are part of
`make test-host`; `run_autoload_tests.py --thread-sanitize` covers concurrent
status/reset/start paths. The PS-button detector remains the existing headless
per-read implementation.

Startup lookup and process listing now share a bounded, validated sysctl snapshot.
Record sizes must advance within the returned bytes, all consumed fields must
fit, and names must terminate inside their SDK field. A malformed snapshot is
rejected in full before exposing a PID/list. Queries retry ENOMEM up to three
attempts and allocate at most 16 MiB. Failed listings become HTTP 500 through
the existing response-error path. SDK assertions preserve the current PID/RSS/
process-name offsets; offset 447 is `ki_comm`, rather than the startup code's
previously misnamed `ki_tdname`. `python3 tests/run_process_tests.py` runs the
actual listing/lookup with synthetic kernel bytes and sanitizers; `--baseline`
reproduces the previous bounds reads and startup loop from `9ffd392`.

SSE log clients now retain independent sequence cursors and partially delivered
frames. Catch-up and live delivery preserve retained ring order even with a
one-byte callback buffer. Multiline text stays one EventSource message; a named
`gap` event reports overwritten entries and is shown in the log UI. Valid
`Last-Event-ID` resumes after a delivered entry within this daemon's log sequence;
invalid/future IDs replay retained history. The ring holds 100 entries in memory,
and cannot recover overwritten logs or logs from a previous process lifetime.
HTTP restarts retain the ring, but old response contexts terminate on generation
change. Every production MHD stop first wakes blocked SSE callbacks, including
standby/network restart and shutdown. Run `python3 tests/run_log_stream_tests.py`
for decoded/bounded callback tests, `--baseline` to reproduce the previous bugs,
or `--thread-sanitize` for shared-state checks. The actual HTTP/SSE fixture is
`python3 tests/run_http_tests.py --logs-only`. Both are part of CI.

The autoload editor waits for both persisted autoload fields before showing
controls or saving. The initial empty config, failed fetches, and incomplete
responses cannot enable autoload or flush an empty list. Run
`node tests/run_autoload_editor_browser_tests.mjs` with Chrome installed
(`CHROME_BIN` overrides the default path). It tests the real React editor with
controlled delayed responses, reloads, navigation, exit saves and list retention;
CI runs this regression too. `--baseline` reproduces the destructive behavior
using the editor from `bfbe474` without changing workspace files.

The editor's external session now owns its draft and serial save queue; rapid
handlers read the latest draft without writing refs during render. Failed or
thrown saves expose Retry, and a failed multi-field request invalidates the
previous acknowledgment because earlier fields may already have been published.
Exit saves use the latest draft without pretending that a queued beacon was
acknowledged. Favorites keep their 250 ms debounce and serialize in-flight writes;
stale config responses cannot overwrite newer local edits. State updater functions
contain no favorite network writes. Countdown/progress values are finite and
clamped, including zero delay. Process/status polling rejects stale results and
cleans up timers, with transport cancellation when AbortController is available
and callback guards on Safari 11 without it. Log scrolling distinguishes reading
history from newly arriving messages. Payload-card action and reorder buttons
are siblings, avoiding nested interactive buttons.

`npm run build` now requires zero-warning lint and all Node tests through
`prebuild`; CI uses that gate and the actual-component Chrome checks.
`cd frontend && npm run test:browser` runs reload retention, the broader
StrictMode/lifecycle regression, and real-DOM markdown/offline/locale checks.
The runner's `--lifecycle-baseline` reproduces
the previous editor's unhandled rejected save using the committed source.

Collected arrays are limited to **4,096 items** and **64 MiB** of allocated array
capacity, whichever is smaller for the item type. Repository parsing accepts up
to **16 MiB** of input text. These bounds apply to the parser/scanner collections;
they do not impose download-stream limits or an aggregate daemon memory budget.
Collection failures become explicit errors rather than successful partial lists.

`.github/workflows/verify.yml` runs all host suites, existing frontend tests,
and gated frontend build without publishing or deploying. Frontend lint has
zero errors/warnings and is required before the build. The host suite tests response
ownership/status handling, request cleanup, and autoload-list bounds. Storage
transactions, autoload worker concurrency, and PS5 firmware behavior remain
separate validation scopes. The main HTTP fixture stubs backend storage/services; the separate
`--config-only` HTTP fixture uses actual config persistence. Successful mock imports move actual files inside temporary fixtures.

The HTTP server accepts config/source bodies up to **1 MiB** and streamed uploads
up to **512 MiB**. Larger declared or accumulated bodies return HTTP 413; empty
request bodies return HTTP 400. Upload staging files use private, unique
`.upload-*` names and are removed by request teardown if uncommitted. This does
not purge older orphan files or guarantee cleanup after power loss/SIGKILL.
Autoload and favorites CSV responses allow 4,095 bytes; larger, unreadable, or
NUL-containing lists return explicit HTTP 500 JSON. Favorites preserve long
lines rather than splitting them into extra CSV entries. JSON builders discard
the complete output after any formatting/escape/capacity error; HTTP converts
serialization failures into complete HTTP 500 JSON.

Response buffer budgets include the terminator: payload/repository lists **4 MiB**,
source lists **512 KiB**, process/history/config **64 KiB**, and logs **154,032
bytes** (enough for the full ring at maximum escape expansion). Metadata uses
64 KiB; source saves allocate a bound derived from `MAX_SOURCES` and field sizes.
These are per-response/serialization budgets; scanner/parser arrays and total
connection concurrency are separate scopes. No unbounded response growth or
automatic repeated scan is introduced.

Native compilation remains a separate gate. To preserve the previous ELF,
build a candidate under `scratch/` rather than run `make clean`:

```bash
mkdir -p scratch
docker run --rm --network=none -v "$(pwd):/src" -w /src \
  ps5-payload-sdk-pldmgr make ELF=scratch/pldmgr-candidate.elf
```

This command assumes the SDK image, frontend distribution, and embedded CA
header already exist. For a new checkout, follow the normal build setup first.
The ELF target currently does not track ordinary header dependencies; use a new
candidate filename or force the ELF target to rebuild after header-only edits.

Markdown emoji use bundled Twemoji PNG sprites. The 3,838 assets share one
20px-tile image embedded in the single HTML file; no CDN or external emoji
requests occur. A postbuild check verifies the PNG is embedded exactly once,
attribution remains and the old Twemoji CDN URL is absent. Source/license/attribution and optional regeneration instructions
are in `frontend/src/assets/emoji/README.md`; builds need no asset download.
Native Unicode emoji can render as missing glyph boxes on PS5, so Unicode is
kept as the safe text fallback when DOM sanitization is unavailable.

DOMPurify first removes untrusted media, classes/styles, forms and scripts while
retaining links/code/tables/headings. Sanitized text nodes produce sprite spans;
a second pass sanitizes the final HTML, including those trusted classes and
accessibility attributes. Loaded notes and browser-cached README responses remain
readable without WAN; this does not add persistent caching or offline uncached
downloads.

All shipped locales are registered, including Dutch. Active-key coverage,
interpolation/React markup, and locale cardinal forms are mandatory Node checks.
Native language names work without Intl.DisplayNames; a scoped i18next cardinal
fallback supports older WebKit without Intl.PluralRules. Locale completeness is
not measured by requiring every value to differ from English.

The actual markdown/locale browser check can also be run separately:

```bash
node tests/run_markdown_locale_browser_tests.mjs
node tests/run_markdown_locale_browser_tests.mjs --baseline
```

The baseline uses the previous committed renderer without modifying source files,
reproducing the CDN dependency and unavailable-DOM raw HTML fallback. The test
blocks external requests; it runs against a local Vite fixture and Chrome.

## HTTP Access Contract

The native daemon serves the UI/API on HTTP port **8084**. It sends approved
payloads to a separate ELF loader on port **9021**; it does not provide that
loader or a login/pairing service. The mock API uses loopback **8081**, and Vite
normally uses **5173** with its same-origin proxy.

| Request class | Method and behavior |
| --- | --- |
| Read APIs | GET: `/list_payloads`, `/get_config`, `/processes_list`, `/history_list`, `/repository_payloads`, `/repository_icons?source=<source-id>`, `/sources_list`, `/autoload_status`, `/log`, `/events`, `/version`, `/getip`, `/manage:check`, `/usb_move_check`. Ordinary repository reads may refresh their cache. Anonymous/foreign autoload status reads do not signal frontend readiness. |
| Explicit mutations | POST: `/manage:upload`, `/manage:delete`, `/loadpayload:<slot-or-path>`, `/set_config`, `/abort`, `/autoload_clear`, `/repository_refresh`, `/repository_cache_clear`, `/repository_install`, `/sources_set`, `/sources_add`, `/sources_remove`, `/usb_move_perform`, `/process_kill`, `/shutdown`. GET does not perform these actions. |
| Preflight | OPTIONS is validated against the requesting origin, route/method and supported headers. Foreign origins do not gain mutation access. |
| Removed updater compatibility | GET `/update_check` returns `{"status":"none"}`. It does not check, download or apply updates. |

A mutation requires an Origin matching the HTTP Host, including port; when
Origin is absent, a matching Referer suffices. Only when **both** are absent can
`X-PLDMGR-Request: 1` authorize it. An explicit foreign Origin or Referer is not
overridden by that marker, and `Sec-Fetch-Site: cross-site` is rejected. Thus an
older same-origin browser window can still make a valid POST without the marker.
The frontend helper supplies the marker for normal actions; same-origin exit
beacons can use the Origin/Referer path.

These are browser CSRF checks, not a secret credential, LAN authentication or
DNS-rebinding protection. A terminal script/direct LAN application can provide
its own headers and perform actions. Read responses are not access-controlled
by a login either. Use a trusted local network; pairing/authentication remains
separate feature work. Do not bypass rejected HTTPS certificate checks or add
wildcard CORS to native mutations as a workaround.

Configuration and lists live in `/data/pldmgr/`: `pldmgr_config.txt`,
`autoload.txt`, `favorites.txt`, `sources.json` and repository caches. Managed
payloads use `/data/pldmgr/payloads/repo/` and `/data/pldmgr/payloads/local/` slots.
USB libraries include `/mnt/usb0/pldmgr` through `/mnt/usb7/pldmgr`; optional root
USB scanning adds `.elf`/`.bin` files on those drives. Launches require supported
regular files within canonical approved roots, and block the manager itself.
Imported/repository slot data is published with staging/recovery; configuration
remaps and multi-file settings updates have the limits described above.

## Hardware Verification

Automated host/browser checks use stubs and local transports. They do not prove
PS5 kernel ABI, native TLS behavior, fonts/layout, rest/resume or the external
loader. Verify these on the target console after runtime changes.

## Repository icon caching

Catalogs use `icons_url` and item-level `icon_key`; see
[CUSTOM_REPOSITORIES.md](CUSTOM_REPOSITORIES.md) for the format and refresh policy.
The daemon stores `repo_icons_<source-url-sha256>.json` and `.meta` alongside
`repo_cache_*.json`. Catalog/icon refresh, icon snapshots and cache clearing share
a mutex. Icon downloads are bounded and validated before atomic publication;
failed updates preserve good data and use a one-hour retry delay.

`GET /repository_icons?source=<configured-id>` serves the local icon catalog.
It cannot fetch an arbitrary requested URL. `POST /repository_cache_clear` uses
the existing mutation-origin policy and deletes only recognized cache filenames.
`python3 tests/run_repository_icons_tests.py` checks the production caching code
with recorded downloads and sanitizers, without contacting a console or WAN.

`node tests/run_markdown_locale_browser_tests.mjs --repository-icons` checks the
actual repository provider, rendered images and Settings action in Chrome, with
all external requests blocked. Mock repository fixtures use `test_repo.json` and
`test_icons.json`.

## Payload Manager self-updates

The official repository alone supplies `pldmgr` updates. Downloads require a
SHA-256 checksum and an embedded `PLDMGR_VER:X.Y.Z` signature matching the
manifest version. The manifest may have a leading `v`; embedded versions must
be numeric stable releases because legacy launchers cannot parse that prefix
or reliably compare prereleases. A newer update is published by atomic rename
to `/data/pldmgr/payloads/pldmgr/pldmgr_vX.Y.Z.elf`, outside managed slots.
Migration preserves both `pldmgr` and `payload-manager` directories, ignoring
case. Startup scans these directories for the highest version strictly above
`MENU_VERSION` and uses a dedicated, symlink-rejecting ELF handoff. Failed
handoffs continue normal startup. Equal-version files remain available for
older initial launchers; startup must not delete them.

`/list_payloads` exposes `self: {id, running_version, pending_version}` separately
from payload slots. A pending version is present only when newer than the
running version. The official catalog uses the higher version for Update
eligibility. Self-updates never use conflict/reinstall/keep-both flows and
remain blocked by regular launch, autoload, USB and upload paths. `/update_check`
remains disabled; there is no restored notification banner or settings toggle.

`tests/run_self_update_tests.py` covers selection, malformed/non-ELF files,
legacy directory preservation, official-only installation, checksum/version
validation, failed commit preservation, pending status, upload/USB guards, and
recorded loader transfers.
It runs under `make test-host` without connecting to a console.

### Changes from the self-update handoff plan

- Keep `repository_entry_valid` strict for ordinary payloads and add a separate
  source-aware validator for self-updates, including refreshed download attempts.
- Require a checksum, a matching embedded version, and a version newer than both
  the running and pending versions; reject explicit storage modes.
- Generate a canonical legacy filename from the embedded version instead of
  trusting the repository filename for the final destination.
- Retain equal/older files at startup. Deleting the currently running update
  would prevent old initial launchers from finding it on their next boot.
  Superseded files in the canonical directory are removed only after publication
  and directory sync; failures retain the previous files and are logged.
- Expose separate self-update status instead of synthesizing a payload slot.
- Translate the download notice in all bundled locales because repository tests
  require complete active-key coverage; English-only fallback would fail them.
