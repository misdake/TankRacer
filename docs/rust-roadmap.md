# Rust rewrite roadmap

This is the short source of truth. Read it before/after each milestone and after
context recovery; check the actual worktree before resuming. Detailed evidence:
[assets](rust-assets.md), [physics](rust-physics.md), [renderer](rust-renderer.md).

## Plan in brief

- Deliver standalone single-player CUP in clean **Rust -> Wasm**, with our own
  raw WebGL renderer. Preserve the runnable C game as the reference.
- Separate immutable resources, authoritative simulation and read-only render
  views. Use world-space meshes, explicit materials, atlases and batching.
- Grow one runnable slice at a time: build/render shell -> static world ->
  physics/collisions -> vehicles/cameras -> race rules/effects -> detachable
  scenery -> opponents/menus/all maps -> optimization and packaging.
- Treat this as a clean rewrite: readable modules and explicit ownership,
  rather than translating the old command stream into a new wrapper.
- Keep physics, update order and RNG exact; allow small visual differences,
  never incorrect blend/depth states. Minor scenery must support config and
  runtime toggles with complete cleanup.
- Measure whole-frame costs before optimizing. Remove repeated work and improve
  data layout; extra per-map data should stay below **1 MB**, never exceed
  **2 MB** resident/uncompressed. The exact accounting rules are below.
- At every completed increment, re-read this plan, verify its runnable exit,
  replace the checkpoint and commit verified work. After context recovery,
  resume from that checkpoint and the actual Git diff.

## Current checkpoint

Updated: **2026-11-14**. Read this together with `git status`.

- **M1/M2 complete; M3 active.** Runnable Rust/Wasm vehicle proxy, terrain and
  live Obstacle/Crush/Fall/Bounce/Closer/Spin scenery; Mover motion is connected.
  Arrows drive; C switches follow/free camera; `/?inspect` stays static.
- **Verified code: `dfe65de`.** Browser keyboard actions now use original Player
  ramps/release and forward/reverse/brake selection. Input: 226 cases / 1,341
  operations match C, including query/error order. Bound input + direct physics:
  eight maps / fifteen cars / 1,968 updates / 12,302,404 matching fields.
  Countdown/fast-start kernel is qualified; browser proxy still starts immediately.
- Original BIO exploding Mover hit, explosion allocation/smoke/retirement and
  debris activation run (`2ef20f7`); scenery: 348 updates / 7,462,914 fields.
  Workspace tests/lints, constructor/prefix/scenery oracles and C preservation
  pass. Browser: 144 GPU cases, direct 120-update and key-driven 24-update native
  final fields, ten short scenery probes and zero idle uploads. Production Wasm:
  **692,497 B**; direct-value exports are test-build only. Explosion sprites/full
  fragment drawing remain pending. Details are in the specialist docs.
- **Next runnable increment:** Player auxiliary brake and parameter restoration:
  bind real grip/rotation/turn overrides to the same drive/model fields, restore
  on release or pickup suppression, qualify strict speed thresholds and short
  hold/release sequences against C, then bind A. `step_keys` currently rejects
  auxiliary input explicitly. Follow with Player collision/progress and required
  startup owners. Preserve direct physics tests; do not expand M3 into all later
  effects or declare it closed from helper tests.
- **Still missing:** full Player/race startup and lifecycle, complete world
  RNG/allocation order, real vehicle rendering, pickups/weapons/race rules,
  optional scenery, opponents/menus/audio and all-map full-game qualification.
  Per-frame browser state parity and full image parity are unqualified.
- **Optimization baseline:** ENGLAND 396 draws / 10,033 triangles. Extra live
  map data: ENGLAND 762,997 B (777,501 B shattering peak), SWAMP 875,170 B,
  BIO 946,548 B. Other maps need accounting; transparent batching/culling remain
  pending. Keep original resources, GPU and dynamic game memory separate.
- Rust output: `build/rust/site`; preserve the C/browser reference. Local
  increment commits are authorized; **do not push** unless asked.

## Incremental delivery

| Stage | Deliverable / exit condition |
| --- | --- |
| M1 done | Rust/Wasm build, browser shell, own raw WebGL renderer, explicit state and GPU tests. |
| M2 done | ENGLAND static scene, categorized resources/materials, atlas/batches and free camera. |
| M3 active | Independent moving proxy: precise terrain, suspension, drive, poses and collision; C module/short-sequence parity. |
| M4 | Vehicle body/turret/tracks/wheels, cameras, shadows and material layers. |
| M5 | Pickups, weapons, destruction, laps, timing and HUD; playable core race. |
| M6 | Cones, signs, birds and ENGLAND train as independent configurable plugins; runtime toggle and complete cleanup. |
| M7 | Opponents, original menus, full CUP and all eight maps. |
| M8 | Profile and optimize CPU/GPU/query/upload/draw work; full-content regression and standalone package. |

Each increment must leave a runnable build. Small changes get focused checks;
large changes get broader checks. Prefer branches and short sequences. Finish
one usable slice before expanding it; keep the static browser runnable during M3.

## Design constraints

1. Clean, readable **Rust -> Wasm -> own raw WebGL renderer**. Separate immutable
   resources, authoritative simulation and read-only render scene. C remains a
   test oracle, never the final runtime.
2. Preserve physics arithmetic, event/update order and RNG. Minor visual changes
   are acceptable; blend, depth, masking and texture-edge errors are not.
   Materials/pipeline states must be explicit and GPU-tested. Keep static meshes
   resident; GPU handles transforms/projection/clipping; batch compatible work.
3. Optional plugins own their entities, colliders, resources, effects, audio and
   subscriptions. Disabling removes behavior; hiding only removes drawing.
   Apply config changes on fixed ticks, record them, and audit shared RNG/order.
4. Extra per-map precomputation: target **<1,000,000 B**; absolute maximum
   **2,000,000 B**, resident/uncompressed including capacity/alignment/metadata.
   Exceeding 1 MB requires convincing whole-frame gains. Account original
   resources and transient/dynamic memory separately.
5. Optimize from first principles and measurements: remove unused/repeated work,
   precompute immutable work, improve layout and batching, then choose caches or
   spatial structures. Preserve query priority/boundaries and exact surface data.

## Mandatory checkpoint routine

Before starting/resuming, read this file and inspect Git status against the last
verified commit. At each completed increment:

1. Re-read the stages and constraints; confirm the increment still serves them.
   Check exact state handling, plugin ownership and the aggregate map budget.
2. Replace the current checkpoint with **what runs, what passed, what is missing,
   and the next runnable step**. Do not grow this file into a chronological log.
3. Put detailed contracts/test counts/commands in the linked specialist docs.
4. Commit verified work, update this checkpoint with the code commit, and re-read
   it before continuing. Keep pending changes distinct; a tested helper alone
   never completes a stage.

After context recovery, resume the pending slice above rather than rebuilding a
plan from chat history. If scope/order changes, update this file first. Do not
let isolated module parity replace progress toward the runnable stage exit.
