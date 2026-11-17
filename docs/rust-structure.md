# Structure checkpoint before M5

Updated 2026-11-17. This increment reorganizes the accepted M4 driving slice;
it does not implement race startup, countdown, pause, pickups, AI or plugins.

## Ownership and dependency boundaries

| Owner | Existing responsibility |
| --- | --- |
| `assets` | File formats and immutable resource metadata, including CUP map order. |
| `game::session::loading` | Shared native/browser preparation of the current resource-built driving slice. Mutable construction ends before creating the session. |
| `game::session::Session` | Platform-independent fixed updates; read-only world/camera/track access for clients. |
| `game::vehicle_runtime` | Existing low-level physics qualification API, immutable session configuration and derived profile. Startup and execution bridges have separate files. |
| `scene` | Read-only extraction, local meshes, attributes, atlases, transparency and material policies. |
| `render::gpu` | GPU resource ownership, explicit states and shared mesh synchronization. |
| `web` / `browser` | Downloads, device adapters, physical input routing, inspection and display submission. |

Native driving and the browser now call the same session tick. Its order is:

1. Advance the original chase camera from the pre-update Player.
2. Publish its integer position and forward axis to the simulation.
3. Run the existing Player/world update through the unchanged scheduler.
4. Advance the existing visual track travel once after success.
5. Publish turret-local feedback for the following camera update.

The `--chase` native path uses this boundary; its camera-free low-level physics
qualification path remains available. Browser redraws only read the track clock.
Existing physics arithmetic, scheduler order and RNG calls are retained.

Inspection has a separate display camera and no path back into the simulation
camera. Logical race commands are independent of the selected display view.
Physical A belongs to either inspection movement or the handbrake at keydown
and keeps that ownership until release/blur; F and auto-repeat cannot reassign
a held key. Arrow/C/V race actions retain their existing meanings.

`SinglePlayer.settings` is private and returns a copy for inspection. Its derived
Profile cannot silently disagree with a caller's newly assigned configuration.
Changing map/car/session configuration constructs a fresh world. The lower-level
`SinglePlayer` still supports construction and numerical test fixtures; the
production `Session` exposes no mutable-world escape hatch.

## Mesh preparation and mutation

Generic preparation accepts optional source-corner attributes and reorders them
alongside winding and surface batches. It knows no vehicle part IDs, BLOB
eligibility or Y=1500 threshold. Those rules and the depth-writing-base constraint
belong to `scene::vehicle::layer` / vehicle preparation. Track UV tables supply
their own period coefficients. Common surface/material policy is in
`scene::material`, independent of browser WorldView.

Vehicle meshes publish revisions at their mutation boundary. Public private-mesh
edits use `Mesh::edit`; the existing physics bridge detects the one-shot body
dent and four gun-Z writes, including partially failed writes. The underlying
kernels receive the same geometry and run in the same order. Scene caches compare
revision/resource identity, not an entire cloned geometry on every draw.
New geometry-mutating behavior must use these ownership boundaries when it is
actually implemented; there is no speculative mutation/event framework.

`Renderer::sync_mesh` owns the repeated upload/reuse/remove logic for vehicles
and scenery. Vertex/index counts come from `GpuMesh`; adapters no longer maintain
another copy. Changed topology uploads its indices even at identical counts;
absent streams are disabled/released, and an empty mesh releases its GPU slot.
Only changed prototypes use this path; poses and ordinary redraws do not.

## Verification and measured scope

- Four saved pre-refactor native outputs match, including a 24-update per-frame
  camera/vehicle/RNG sequence, direct/key/handbrake and camera-free qualification.
- A separate manual-order oracle compares 96 updates across ENGLAND/BIO and both
  control paths: physical/input/prefix/scenery state, RNG, camera, tracks and
  current geometry/revisions. A fault case checks invalidation after partial
  gun writes; it does not change the kernel's error behavior.
- Existing vehicle/scenery C reference suites pass: 2,880 and 348 updates,
  respectively. Vehicle assembly covers 60 initializations; camera 192 updates;
  layer/aerial 2,772 fields; original x86 track UVs 90 cases / 11,880 GPU corners.
  These are the affected established suites, not a new long-race test.
- Workspace tests (105 passed; 65 external-reference tests explicitly ignored by
  the ordinary entry), input ownership checks, native/Wasm lint, browser regression
  and 191 GPU cases pass. Four added GPU cases exercise shared-cache topology,
  optional streams and cleanup. Production and test-feature builds are linted
  separately. Inspection on/off beside a BIO Mover compares exact diagnostic
  state at each of 24 updates while the display camera moves far away; the Mover
  remains active and every compared state is identical.
- The default frame remains 386 draws / 10,169 triangles / 29 pipeline changes.
  Idle uploads remain zero. Current vehicle render caches are 49,251 B; original
  resources, authoritative mesh mutation metadata and GPU storage are separate.
  Extra resident map probes: ENGLAND 769,493 B (785,837 B shatter peak), SWAMP
  881,042 B, BIO 944,684 B. These are measured views, not all-map peak bounds.

## Selective test entry points

```sh
node tools/test_rust.mjs unit
node tools/test_rust.mjs lint
node tools/test_rust.mjs web
node tools/test_rust.mjs reference vehicle-runtime scenery-runtime vehicle-model
node tools/test_rust.mjs reference camera vehicle-layers
```

`npm run test:rust` runs unit/lint/web and explicitly reports that external
references were not selected. `npm run test:rust:reference -- <modules>` selects
references. Set `CC` to a GCC-compatible compiler if `gcc` is not on PATH. The
runner verifies source/archive existence, rebuilds missing/stale C archives,
compiles selected fixtures and records hashes/results in
`build/rust/tests/references.json`. An explicitly selected missing prerequisite
fails; it never silently skips. The `tracks` reference additionally requires
`TANKRACER_ORIGINAL_EXE` and Python `pefile`/`unicorn`.

Logs/results for this increment use `build/rust/tests/structure-*`,
`references-structure-c.json` and the usual browser report. C source preservation
still passes (97 modules / 244 unchanged files / 2 audio packs). Production
Wasm is 759,135 B and excludes test-only probes. No publication/push is part of
this increment.

## Next integration point

M5 can extend the actual session's startup and tick with a real race owner, then
countdown, checkpoints/laps/timing and finish/restart/exit. Selection and existing
device/resource boundaries already provide the required entry points. No dummy
race states, pause handlers, plugin lifecycle, opponent manager or unused generic
engine APIs were added ahead of their implementations.
