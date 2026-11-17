# Changelog

What changed for players, newest first: the start page's Changelog button shows this file (web/build.mjs turns it
into /changelog.json).

Each day with a release gets one section, written when its module is built: `## <date> (<commit>)`, the commit being
the last one the day's releases bring. One line per change, in plain text, as `- **Area:** what changed`.

## 2026-11-18 (d435799)

- **Shaders:** ProStreet's compiled D3D9 shaders translate to WGSL and validate on WebGPU (591 of 591), reusing the
  NFSU2 web pipeline.
