# Security policy

## What counts as a vulnerability here

This project runs translated game code in a browser sandbox. Reports that are
in scope:

* guest-controlled values that escape the guest address space (a bad guest
  pointer must never become an arbitrary host read/write),
* the wasm/JS boundary: an asset, save file or network message that makes the
  runtime execute host JavaScript or escape into the page,
* the local build pipeline: a crafted `nfsps.exe` (or data file) that makes the
  lifter or a script write outside its output directory, execute a shell, or
  produce arbitrary host code,
* dependency or CI compromise affecting released artifacts.

Out of scope: game bugs, crashes of the original executable, server-side issues
of the future online replacement (report them once that code exists), and
anything that requires the attacker to already run code on your machine.

## Reporting

Use GitHub's private vulnerability reporting on this repository
(**Security → Report a vulnerability**). If that is unavailable, email the
maintainer at the address in the commit history and put `SECURITY` in the
subject.

Please do not attach the game executable, game data, or generated C to a
report — describe the input that triggers the issue or share a minimal,
non-infringing file.

Include: affected commit, platform, steps, and what you observed. We aim to
acknowledge within a week and to publish a fix and credit unless you ask
otherwise.
