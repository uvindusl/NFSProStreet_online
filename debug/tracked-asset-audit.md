# ProStreet tracked-asset audit

## Method

Ran `git ls-files` (429 tracked files), classified each as text or binary (NUL byte scan: 14 binary, 415 text). For each binary/blob recorded size, add-commit (`git log --diff-filter=A --oneline -- <path>`), and references (grep in `web/` and `lifter/`).

## Binary/blob inventory (14 files)

| Path | Size | Added by | Referenced by | Verdict |
|------|------|----------|---------------|---------|
| analysis/webgpu-test/tri.png | 30608 | b9e0fb0 — "evidence: E4 WebGPU triangle proof artifacts" | tri_capture.js / tri_capture2.js (output path) | **generated** — WebGPU triangle screenshot, proof artifact, not game data |
| lifter/runtime/flags.dat | 230088 | 8e57fc1 — "track build-critical inputs so E3 reproduces" | roam_game.c (#embed), flags.ts (doc) | **generated-from-third-party** — built from flag-icons SVGs (github.com/lipis/flag-icons, MIT) + Wikimedia Commons Quebec SVG, drawn in Chrome; reads NO game file |
| web/runtime/web/runtime/flags.dat | 230088 | 7b0d6b4 — "track build-critical sources" | roam_game.c (#embed), flags.ts (doc) | **generated-from-third-party** — duplicate copy of lifter/runtime/flags.dat (identical bytes), for web runtime build |
| web/src/app/drift.png | 4870 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | app/style.css (mask-image) | **source** — UI indicator icon (drift mask), not game data |
| web/src/app/fmbot.png | 1716 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | app/style.css (mask for .fmbot-mark) | **source** — github.com/fmbot-discord/fmbot logo, not game data |
| web/src/app/nitro.png | 3433 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | app/style.css (mask-image) | **source** — UI indicator icon (nitro mask), not game data |
| web/src/app/status.png | 433 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | app/style.css (mask-image) | **source** — UI indicator icon (status mask), not game data |
| web/src/static/favicon-16.png | 410 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | manifest.json | **source** — favicon, not game data |
| web/src/static/favicon-32.png | 1077 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | manifest.json | **source** — favicon, not game data |
| web/src/static/horns.dat | 662486 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | horn.c (#embed), horn-preview.ts (fetch) | **generated-from-third-party** — cut from YouTube recordings with ffmpeg; reads NO game file |
| web/src/static/icon-180.png | 6699 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | manifest.json | **source** — PWA icon, not game data |
| web/src/static/icon-192.png | 8628 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | manifest.json | **source** — PWA icon, not game data |
| web/src/static/icon-512.png | 28435 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | manifest.json | **source** — PWA icon, not game data |
| web/src/static/icon-maskable-512.png | 23980 | 45beb33 — "import browser host + runtime glue from nfsu2-web reference" | manifest.json | **source** — PWA maskable icon, not game data |

## Game-asset / game-derived files: NONE

No tracked file is genuinely derived from EA game data. Specifically:

- **flags.dat** (both copies): built from flag-icons SVGs (github.com/lipis/flag-icons, MIT) and one Wikimedia Commons public-domain SVG (Quebec), drawn in Chrome via Playwright. `mkflags.mjs` reads NO game file.
- **horns.dat**: cut from YouTube recordings with ffmpeg (`yt-dlp -x --audio-format wav`). `mkhorns.mjs` reads NO game file.
- All PNGs: UI icons, favicons, PWA icons, fmbot logo — none extracted from game data.
- tri.png: WebGPU proof screenshot — generated proof artifact, not game data.

## Missing tooling credits

The build scripts `mkflags.mjs` and `mkhorns.mjs` are **not tracked in this repo**. They exist only in `reference/nfsu2-web/web/tools/` which is not a tracked path. `THIRD_PARTY.md` does not credit flag-icons (github.com/lipis/flag-icons) or Wikimedia Commons.

## Read-only audit

No files were modified or deleted.
