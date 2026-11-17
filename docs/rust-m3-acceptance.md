# M3 acceptance: independent moving physics

Accepted 2026-11-14. Code commit is recorded in the [roadmap](rust-roadmap.md).
The fixed scope is the original moving proxy: terrain -> contact/suspension ->
drive/pose -> collision, with C module/short-sequence parity. The complete game
remains the full objective, with later deliverables in [milestones](rust-milestones.md).

## Delivered and checked

| Requirement | Evidence |
| --- | --- |
| Independent Rust runtime | Fresh resources construct the native `drive` example and browser Wasm session; neither links the C reference or uses a dump/emulator/server simulation API. |
| Connected physics | Terrain/material/contact/suspension, powertrain, attitude/poses, obstacle/tank/deformable/ground collision and motion run through canonical owned state. |
| Actual input | Fixed-tick keyboard ramp/release/reverse, handbrake/restoration and collision indication are bound; browser DOM A press/release passes. |
| Current scene integration | Obstacle/Crush/Fall/Bounce/Closer/Spin/Mover, relevant fragment/blast state, shared registry/factory/scheduler/queues and RNG run in the current supported scene slice. |
| C qualification | Fresh runtime: 2,880 updates / 18,041,268 fields across eight maps and fifteen cars. Connected multi-vehicle fixture: 1,254 records / 17,591,969 words, with actual peer writes and shared effects. |
| Current WayPoint tail | WP02: 774 records / 29,412 queries / 102,544 words. All 24 B/S/G resources are checked; 22 have circuits and the two unused CIRCUIT B/S files exercise empty-link error -4. |
| Shared scene after routes | SR07: 348 updates / 8,103,619 fields match C, including route IDs/poses/links/weather/grid and scheduler membership. |
| Browser/GPU | 144 pixel cases, eight depth comparisons, 81 blend transitions, native/Wasm 120-update direct input and 24-update key/brake final fields, ten short scenery probes and zero idle uploads pass. |
| Build/source preservation | Workspace tests, native and Wasm lints, test and production builds pass. 97 C modules / 244 source files and two audio packs remain unchanged. |
| Runnable checkpoint | Native command and static browser package below; local code/checkpoint commits, no push. |

The closing native regression also passes scene loading/objects, vehicle startup,
runtime/full connected physics/prefix, Mover/Spin/Closer/Bounce, mesh break/shatter,
blast and Player input/brake/collision suites. Logs are under
`build/tests/rust-m3-close-*.log`; browser detail is `build/rust/tests/browser.json`.
This is one stage-level regression, not a requirement to repeat all suites after
every small change. The fresh runtime test includes 24-update sequences and its
existing two 96-update direct-physics cases, not whole natural races.

## Memory and renderer baseline

Measured resident/uncompressed extra map data in the actual Wasm renderer:

| Map | Render/bookkeeping | Collision | Route records/grid/weather | Total |
| --- | ---: | ---: | ---: | ---: |
| ENGLAND | 501,275 B | 261,730 B | 4,400 B | **767,405 B** |
| SWAMP | 609,626 B | 265,552 B | 4,416 B | **879,594 B** |
| BIO | 664,446 B | 282,110 B | 2,864 B | **949,420 B** |

ENGLAND's tested shattering peak is 781,909 B. Route accounting conservatively
includes the whole typed route container, sparse-record capacity, ordered grid
lists, inline weather table and Vec metadata; base entity/pose state remains
ordinary dynamic gameplay memory. Route storage is a separate counter and the
test checks render + collision + route equals the aggregate. All tested totals
are below the preferred 1,000,000 B target; the 2,000,000 B runtime guard remains.
Other maps' full live render aggregates remain for later map acceptance.

Default ENGLAND remains 396 draws / 10,033 triangles, 22 pipeline changes and
zero geometry uploads at rest. Transparent batching/culling and full-frame
optimization are still M8 work. Production Wasm is **703,997 B**; controlled
load/inspection/direct-drive/GPU probes are absent from production exports.

## Explicit limits and remaining owners

- The player is a box with a proxy chase/free camera. Actual vehicle meshes,
  tracks/wheels, original cameras, shadows and material layers are M4.
- The C oracle is a test boundary. Fresh-runtime fixtures stop before later
  session initialization; scene tests use the supported-class script and direct
  Player physics slice. Full original world allocation/RNG startup and complete
  Player/race updates are not qualified by these counts.
- WayPoint construction, forward-symbol resolution, branch marking, circuit
  numbering, weather carry across laps and ordered queries are implemented.
  Player lap/progress controllers do not consume them yet. These are M5.
- Pickup/weapon variants, full effect presentation and HUD, countdown timing,
  finish/restart/exit and complete Player lifecycle remain M5. Qualified fade,
  shield, temporary-effect and fragment kernels are foundations, not delivered
  interactions. The proxy's race-active flag starts true.
- Config/runtime optional modules and complete teardown are M6; opponents,
  original menus/audio/full CUP/all maps are M7; full optimization/package is M8.
- Matching browser final fields and GPU cases do not imply per-frame browser/C
  equality, full image parity or a playable full race.

## Run and reproduce

```powershell
node tools/build_rust.mjs
node tools/serve.mjs 8133 build/rust/site
cargo run -p tankracer-game --example drive --release --locked -- assets/game 0 1 24 --scenery --keys
npm run test:rust
python tools/verify_sources.py
```

Open `http://127.0.0.1:8133`: arrows drive, A handbrakes, C switches follow/free
camera; `/?inspect` retains static inspection. The native runner accepts map
indices 0..7 and vehicle 1..15. Browser production loads ENGLAND; SWAMP/BIO are
controlled test views, not a shipped map-selection menu.

Native C oracles must be built before running their ignored Rust tests. On the
verified Windows host, after a native C build, the route/scene tail is reproduced by:

```powershell
& gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_waypoint.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/waypoint_reference.exe
& gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scenery_runtime.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/scenery_runtime_reference.exe
$env:TANKRACER_WAYPOINT_REFERENCE=(Resolve-Path build/native/waypoint_reference.exe).Path
$env:TANKRACER_SCENERY_RUNTIME_REFERENCE=(Resolve-Path build/native/scenery_runtime_reference.exe).Path
cargo test -p tankracer-game --test waypoint_reference --test scenery_runtime_reference --locked -- --ignored --nocapture
```
