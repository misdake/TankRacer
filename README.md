# Tank Racer CUP

A Rust rewrite is in progress, starting with a small world-space WebGL 2
renderer. The runnable C game below remains the reference during migration.
The short plan and current checkpoint are in [the roadmap](docs/rust-roadmap.md),
with fixed deliverables in [the milestone plan](docs/rust-milestones.md).
Re-read it after each completed increment and after context recovery; the
repository instructions in `AGENTS.md` require this checkpoint routine.

```sh
rustup target add wasm32-unknown-unknown
node tools/build_rust.mjs
node tools/serve.mjs 8133 build/rust/site
```

Open http://127.0.0.1:8133. The Rust build runs precise terrain/vehicle physics
in Wasm, with authored body/turret/gun and animated tracks/wheels on ENGLAND.
Arrow keys drive; A applies the original handbrake; C cycles the original
near/far/cockpit views; hold V to look back. F toggles free inspection (drag to
look, WASD to move, Q/E down/up, Shift to move faster).
Keyboard actions now use the original Player throttle/brake/steering rates and
forward/reverse switching. The proxy still starts immediately; the input kernel
also supports countdown/fast-start rules for the later race controller.
Use `/?inspect` for static-world inspection. Authored Obstacle/Crush/Fall/Bounce/Closer/Spin/Mover
actors run from live state; WayPoint graphs/grid/weather are loaded for later
race integration. **M1-M3 are accepted:** the independent moving-physics proxy
is delivered. [M3 acceptance](docs/rust-m3-acceptance.md) records exact evidence
and limits. **M4 is accepted for the driving scene:** [vehicle rendering](docs/rust-m4-vehicle.md)
includes animated tracks/wheels, ground/body shadows, original chase/cockpit
views, a four-segment aerial and the authored additive normal layer. Physics
updates are unchanged. Next is M5 race ownership and lifecycle.
Full Player/race
startup, pickups/weapons, optional content, AI, menus/audio and full CUP remain
later milestones. Rust 1.95,
Node.js 22+ and `wasm-pack` are required. `Cargo.lock` fixes crate versions.
Run `npm run test:rust` for native resource tests, lint checks and actual browser
GPU pixel tests (set `CHROME` if Chromium is not in a standard location).
GPU probes compile only with `gpu-tests`; test builds have a separate output.
The [structure checkpoint](docs/rust-structure.md) documents session ownership,
inspection isolation, mesh caches and selective verification commands. C/original
reference tests are selected explicitly with `npm run test:rust:reference -- <modules>`;
the ordinary check reports them as not selected.

The native Rust probe can load the same supported authored collision scenery
before placing the player:

```sh
cargo run -p tankracer-game --example drive --release --locked -- assets/game 0 1 120 --scenery
```

Add `--keys` to exercise the same Player keyboard path used by the browser;
without it, the example supplies direct physics inputs for qualification.
Add `--chase` to use the same authoritative session/camera/feedback sequence as
the browser. F inspection never supplies a camera to that simulation sequence.

It reports loaded/deferred classes explicitly. Both paths still omit complete
race rules and the deferred subclasses.
Mover construction/movement and original BIO exploding hits are connected,
including explosion/smoke updates, vehicle impulse and debris activation.
Explosion sprites and complete fragment drawing remain pending.

See [the renderer notes](docs/rust-renderer.md) for the current architecture.

## C reference

Single-player Tank Racer, recompiled from readable C reconstructed from the original game. The C core owns physics, opponents, world updates, CUP menus and drawing commands. The browser supplies WebGL rendering, Web Audio and keyboard input.

The repository is self-contained: all eight CUP tracks, vehicle resources, menus and 117 sound samples are included. No original executable, memory dump, Unicorn, Ghidra or Python server is required to play.

## Build and play

Build tools: Python 3.10+, a GCC-compatible C compiler with `ar`, Node.js 22+ and an installed Emscripten SDK. Emscripten 7.0.0 is the verified version. Python is used only for building, packaging and source verification. There are no npm dependencies to install.

```sh
python tools/build.py --cc /path/to/gcc --emsdk /path/to/emsdk --package
node tools/serve.mjs
```

Open http://127.0.0.1:8124. Serve `dist/` on any static web server to deploy; `build/TankRacerCUP-web.zip` contains the same site. The included local server negotiates gzip for Wasm, game archives, audio packs and other compressible resources.

`CC`, `AR` and `EMSDK` environment variables can replace the command-line tool paths. Use `--target native` or `--target wasm` to build one backend. Build outputs live in ignored `build/` and `dist/` directories. Toolchains live outside this repository.

Native builds also support CMake:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --parallel
```

Select the local compiler/generator through the usual CMake options if it is not in `PATH`.

## Native integration

`host/game_host.h` exposes the same API exported to Wasm: create a game, mount seven immutable archives, start, then supply input and frame counts. `tank_step` accepts 0–8 fixed 16 ms updates; zero updates redraw without advancing gameplay. Output comprises an NWF1 packet and a separate audio command list. The API is single-instance and must be called serially.

`build/native/tankracer` (`.exe` on Windows) is a stdin/stdout host. Run it from this directory, or pass an absolute resource root:

```sh
build/native/tankracer --assets assets/game --map ENGLAND --cup B
```

Each input line is `frame_count hexadecimal_buttons focused`, for example `1 1 1`. Stdout is binary NWF1; diagnostics use stderr. `--snapshot` wraps each packet in two little-endian 32-bit lengths (packet, game data), followed by the packet and all 8 MiB of game data. This option is intended for short parity tests. `--help` lists selection options. The CLI consumes direct race inputs; the browser uses the additional C frontend/menu API.

## Tests

```sh
python tools/verify_sources.py
node tests/parity.mjs
node tests/cup.mjs
node tests/site.mjs
```

Or use `npm test` after building both backends. Tests compare native/Wasm full game data, command buffers and textures on short input sequences across all eight tracks; exercise pause, restart, all 15 CUP race/result/reload transitions, initials, unlocks and qualification failure; and verify static gzip responses and every audio sample. Results are written under `build/tests/`. Finish-branch tests deliberately set finish inputs, rather than driving whole races.

For a release/export regression, optionally run `node tests/baseline.mjs /path/to/previous/site/wasm`. It compares complete game/menu memory, drawing and audio commands against the earlier Wasm build, excluding host timing fields. Normal builds and tests do not need an earlier checkout.

## Layout

| Path | Contents |
| --- | --- |
| `src/` | 97 readable C modules and their required headers/tables |
| `host/` | Production memory/resource/platform adapters, public API and native CLI |
| `web/` | Browser source and local Bootstrap assets |
| `assets/game/` | 15 immutable archives, including all eight CUP tracks |
| `assets/sounds/`, `assets/audio.json` | Two audio packs and sample/resource catalog |
| `tools/` | Portable build, packaging, gzip server and provenance check |
| `tests/` | Short backend parity, CUP lifecycle and resource tests |
| `docs/` | Architecture and integration notes |

See [architecture](docs/architecture.md) for the update/render boundary, input bits and packet format. `source-manifest.json` records the export origins and hashes. Reverse-engineering tools, original executables, dumps, recordings, historical tests, obsolete pages and compiler installations remain in the research workspace, outside this repository.

Original game resources are retained in their existing archive/packed formats. This export does not assign a new license to those resources. Third-party notices are included in `web/vendor/`.
