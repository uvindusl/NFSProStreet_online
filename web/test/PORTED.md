# Ported Web Tests

This directory contains unit tests ported from `reference/nfsu2-web/web/test/` to make
`npm run test:fast` work in this port without pulling in NFSU2-only server, network,
or roam features.

## Copied files

| File | Reason |
|------|--------|
| astc.test.mjs | Pure DXT1-to-ASTC decoder test; target `web/src/gpu/astc.ts` exists |
| dxt.test.mjs | Pure DXT unpacking test; target `web/src/gpu/dxt.ts` exists |
| gencheck.test.mjs | Tests `web/tools/gencheck.sh` and `tools/genstamp.sh` against the live checkout |
| inflate.test.mjs | Tests `runtime/web/inflate.h` raw deflate inflator; target exists |
| memcmp.test.mjs | Tests `runtime/web/memcmp.c` via wasi-sdk; target exists |
| memcmp_unit.c | C unit source for memcmp/bcmp wasm test |
| runtime-unit.test.mjs | Native runtime unit tests (macOS/clang only, skipped on Linux) |
| unit-tests.mjs | Test runner: discovers `web/test/*.test.mjs` and runs under `node --test` |
| wasi-run.mjs | Wasm runner helper required by memcmp.test.mjs |

## Deliberately excluded files

| File | Reason |
|------|--------|
| alias.test.mjs | Requires NFSU2 alias/cheat code system not present |
| altgr.test.mjs | Requires NFSU2 keyboard input system not present |
| app-session.test.mjs | Requires NFSU2 app/session management not present |
| boot.test.mjs | Requires NFSU2 boot flow not present |
| crews.test.mjs | NFSU2 multiplayer crew system - not in port |
| data-blocks.test.mjs | NFSU2 texture pack blocks - not in port |
| deploy.test.mjs | NFSU2 deploy pipeline - not in port |
| devicelog.test.mjs | NFSU2 device logging not present |
| docker.test.mjs | NFSU2 docker integration not present |
| flags.test.mjs | NFSU2 flag graphics system not present |
| friends.test.mjs | NFSU2 friends system - not in port |
| gps-distance.test.mjs | NFSU2 GPS/distance feature not present |
| hidpad.test.mjs | NFSU2 gamepad support not present |
| horn-preview.test.mjs | NFSU2 horn preview not present |
| i18n.test.mjs | NFSU2 localization system not present |
| input-chars.test.mjs | NFSU2 input character handling not present |
| input_unit.c | NFSU2 input unit test - requires NFSU2 input runtime |
| invite.test.mjs | NFSU2 invite system not present |
| io-kept.test.mjs | NFSU2 I/O keepalive not present |
| io-stuck.test.mjs | NFSU2 I/O stuck detection not present |
| lan.test.mjs | NFSU2 LAN multiplayer not present |
| lan_unit.c | NFSU2 LAN unit test - requires NFSU2 LAN runtime |
| lan-wasm.test.mjs | NFSU2 LAN wasm not present |
| lastfm.test.mjs | NFSU2 Last.fm integration not present |
| lastgame.test.mjs | NFSU2 last-game tracking not present |
| local-pads.test.mjs | NFSU2 local gamepad pads not present |
| log-english.test.mjs | NFSU2 English log messages not present |
| logs.test.mjs | NFSU2 log system not present |
| menu-layout.test.mjs | NFSU2 menu layout not present |
| messages.test.mjs | NFSU2 message system not present |
| mods.test.mjs | NFSU2 mods system not present |
| module.test.mjs | NFSU2 module system not present |
| movies-av1.test.mjs | NFSU2 AV1 movie playback not present |
| names.test.mjs | NFSU2 name system not present |
| net-clock.test.mjs | NFSU2 network clock not present |
| net-crews.test.mjs | NFSU2 network crews - not in port |
| net-proto.test.mjs | NFSU2 network protocol - not in port |
| net-room.test.mjs | NFSU2 network room - not in port |
| net-trouble.test.mjs | NFSU2 network trouble - not in port |
| net-worker.test.mjs | NFSU2 network worker - not in port |
| packs.test.mjs | NFSU2 texture packs not present |
| padclick.test.mjs | NFSU2 pad click not present |
| page-audit.test.mjs | NFSU2 page audit not present |
| page-lifecycle.test.mjs | NFSU2 page lifecycle not present |
| page-narrow.test.mjs | NFSU2 page narrow mode not present |
| pcsave.test.mjs | NFSU2 PC save system not present |
| phonegraphics.test.mjs | NFSU2 phone graphics not present |
| photolook.test.mjs | NFSU2 photo look not present |
| phrases.test.mjs | NFSU2 phrase system not present |
| plates.test.mjs | NFSU2 license plates not present |
| players.test.mjs | NFSU2 players system not present |
| playtime.test.mjs | NFSU2 playtime tracking not present |
| poselog_unit.c | NFSU2 pose logging not present |
| prefs.test.mjs | NFSU2 preferences not present |
| privacy.test.mjs | NFSU2 privacy settings not present |
| profile-names.test.mjs | NFSU2 profile names not present |
| quickchat.test.mjs | NFSU2 quick chat not present |
| radio.test.mjs | NFSU2 radio not present |
| rendercheck.test.mjs | NFSU2 render check not present |
| restore-limit.test.mjs | NFSU2 restore limit not present |
| roam-asi.test.mjs | NFSU2 roam ASI plugin not present |
| roam-block.test.mjs | NFSU2 roam block not present |
| roam_hubwire.c | NFSU2 roam hub wire not present |
| roam_selftest_main.c | NFSU2 roam selftest not present |
| roam-wasm.test.mjs | NFSU2 roam wasm not present |
| savegone.test.mjs | NFSU2 save gone not present |
| saves-review.test.mjs | NFSU2 saves review not present |
| sbrk_window_unit.c | NFSU2 sbrk window not present |
| settings-diff.test.mjs | NFSU2 settings diff not present |
| settings-report.test.mjs | NFSU2 settings report not present |
| settings-reset.test.mjs | NFSU2 settings reset not present |
| sitekeys.test.mjs | NFSU2 site keys not present |
| songs.test.mjs | NFSU2 songs not present |
| start-sh.test.mjs | NFSU2 start script not present |
| steampad.test.mjs | NFSU2 steam pad not present |
| suite-module.test.mjs | NFSU2 suite module not present |
| sw.test.mjs | NFSU2 service worker not present |
| texpacks.test.mjs | NFSU2 texture packs not present |
| textentry.test.mjs | NFSU2 text entry not present |
| touch.test.mjs | NFSU2 touch input not present |
| tracks.test.mjs | NFSU2 tracks not present |
| vault-box.test.mjs | NFSU2 vault box not present |
| vault.test.mjs | NFSU2 vault not present |
| viewport.test.mjs | NFSU2 viewport not present |
| wheel.test.mjs | NFSU2 wheel input not present |
| whine.test.mjs | NFSU2 whine not present |
| worlds.test.mjs | NFSU2 worlds not present |
