# Payload Manager frontend

The React dashboard is bundled into a single HTML file embedded in the native
PS5 daemon. Vite targets `es2015` and Safari 11. Build/deployment and the HTTP
security contract are documented in [DEVELOPMENT.md](../DEVELOPMENT.md).

Run these commands from `frontend/`:

```bash
npm ci
npm run dev:mock
```

The mock API listens on loopback port 8081; Vite prints the UI address (normally
5173). `npm run dev` alone expects an API on 8081 via Vite's proxy. The native
PS5 dashboard uses port 8084. Mock process/storage/loader responses are fixtures;
they do not prove that hardware services work.

```bash
npm run build
npm run test:browser
```

`build` runs zero-warning lint and Node tests before Vite, then verifies the
production emoji PNG is embedded exactly once, attribution remains, and the old
CDN dependency is absent. `test:browser` requires local Chrome (`CHROME_BIN` may
override its path) and exercises actual editor reloads, StrictMode/save queues,
markdown DOM sanitization, offline rendering and language selection. The separate
mutation-browser/native-fixture checks and host sanitizers run in CI; see the
root development guide for their commands and limits.

Localization instructions are in [TRANSLATIONS.md](../TRANSLATIONS.md).
Markdown uses bundled PNG sprites because native emoji fonts failed on PS5.
Sprite position rules are added only when needed, and both sanitizer passes
exclude arbitrary remote media. Asset regeneration is optional; ordinary builds
use the bundled files and need no graphics download or Pillow.
