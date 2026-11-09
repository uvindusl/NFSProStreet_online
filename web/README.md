# ProStreet browser host

The local host follows the same shape as `nfsu2-web`: it serves the browser
shell and WASM module, then exposes a filtered view of a user-owned game
directory through HTTP.

```sh
web/start.sh --data "/path/to/Need for Speed ProStreet/app"
```

Open `http://127.0.0.1:8080/`. The server provides:

* `/game.json` — a manifest of allowed game files;
* `/data/<path>` — case-insensitive, read-only game-file access;
* `/prostreet.wasm` — the module from `out/web/`.

The current WASM module is still diagnostic and does not execute the game or
request `/data` files yet. This host is the foundation for the S2 runtime
integration; later it can be replaced or fronted by a Rust server for HTTPS
and multiplayer deployment.
