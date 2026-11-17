# Rust rewrite roadmap

This is the short source of truth. Read it before/after each milestone and after
context recovery; check the actual worktree before resuming. Detailed evidence:
[assets](rust-assets.md), [physics](rust-physics.md), [renderer](rust-renderer.md).
Deliverables and fixed acceptance boundaries: [milestones](rust-milestones.md).

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

Updated: **2026-11-17**. Read this together with `git status`.

- **Accepted foundation:** M1-M4 within their fixed scopes; pre-M5 ownership
  cleanup at `7c93bbc` (review `a150546`). The shared native/browser Session owns
  one update sequence; inspection is read-only; configuration/terrain/surfaces
  and mesh/material/GPU cache boundaries are retained. See [M4](rust-m4-vehicle.md),
  [M3 scope](rust-m3-acceptance.md) and [structure](rust-structure.md).
- **M5.1a-c accepted:** Player route binding; ordered cold construction at
  `8612f7c`; countdown/launch/raw race clock at `b09dabb`. All 22 usable CUP files
  use the fallback Player; full constructor RNG/IDs/selected actor state match C.
  `startup` runs standalone in Rust; its complete actor world remains read-only
  because optional ticks have not been integrated. Missing methods fail, never
  silently update. No RNG skipping or identity padding is permitted.
- **Current code checkpoint: M5.2a HUD accepted at `ef5adf9`.** The normal
  browser runs original 3000 ms countdown, launch rules, race time and source-
  derived fonts/dial/map/countdown/GO/timer/speed/cockpit indicator on the accepted
  driving subset. That subset retains its own constructor seed; it is not a
  claim of complete-world/frame parity. Physics kernels and camera/world/
  animation/feedback order are unchanged. Free inspection cannot alter gameplay.
  Lap one/rank one reflects the current slice; inventory, lap records and
  warning events are not fabricated.
- **Current verification:** HUD C/native: 128 snapshots / 88,384 geometry/UV/
  color/state fields across eight maps. Clock C/native: 144 updates / 941,304
  fields; native/Wasm: 12 scene updates. Existing vehicle C regression: 2,880
  updates / 18,041,268 fields; four saved driving checkpoints remain unchanged.
  Browser overlay pixels, three ordered atlas draws, stationary upload reuse,
  state/time/RNG isolation and 191 GPU cases pass. Workspace 114 passed / 69
  external ignored; lints/production pass. Wasm 836,272 B, without test exports.
  Original production C/host/web/assets are preserved. HUD resource pixels are
  released after upload; no extra map query/precompute data was added. Detailed
  commands, coverage and remaining boundaries: [M5 evidence](rust-m5-race.md).
- **Next runnable slice: M5.2b player actions.** Bind original Z/X turret
  acceleration/centering to the existing visible poses; test branches and short
  C/native/Wasm sequences. Then real inventory/fire/projectile/pickup ownership,
  pickup -> use -> hit -> cleanup, effects/HUD consumers. M5.2 is authorized in
  this run and remains open; dependencies require real owners, not stubs.
- **Remaining owners:** M5.1d checkpoint/lap/restoration/finish/restart/exit;
  M5.2 weapons, pickups, damage, effects and remaining HUD/start-camera feedback;
  M6 optional scenery config/ticks/cleanup; M7 opponents, original menus/audio/
  complete CUP and all-map presentation; M8 whole-frame optimization/package.
  Full race, complete-world updates and image parity are still unqualified.
- **Measured baseline:** normal vehicle with aerial 146 triangles / 12 draws,
  plus 2 shadow triangles / 1 draw; cockpit 32 mesh + 2 sight triangles / 2 draws.
  The 120-update ENGLAND view measures **386 draws / 10,169 triangles / 29 pipeline
  changes**. Aerial costs one draw and at most 256 B of changed instance matrices;
  the normal layer adds no draws/per-pose vertex uploads. Vehicle caches:
  **49,251 B**, separately counted; ordinary vertex layout remains 44 B.
  Current extra resident map probes: ENGLAND **769,493 B** (785,837 B shattering
  peak), SWAMP **881,042 B**, BIO **944,684 B**; all below 1 MB. These are measured
  views, not all-camera/all-map peak qualification. Transparent batching/culling
  remain pending. Keep resources, GPU and dynamic game memory separate.
- Rust output: `build/rust/site`; preserve the C/browser reference. Local
  increment commits are authorized; **do not push** unless asked.

## Incremental delivery

| Stage | Deliverable / exit condition |
| --- | --- |
| M1 done | Rust/Wasm build, browser shell, own raw WebGL renderer, explicit state and GPU tests. |
| M2 done | ENGLAND static scene, categorized resources/materials, atlas/batches and free camera. |
| M3 done | Independent moving proxy: precise terrain, suspension, drive, poses and collision; C module/short-sequence parity. [Acceptance](rust-m3-acceptance.md). |
| M4 done for the driving scene | Authored parts/animation, ground/body shadows, original chase/cockpit, aerial and BLOB normal layer. [Evidence](rust-m4-vehicle.md). |
| M5 | Pickups, weapons, destruction, laps, timing and HUD; playable core race. |
| M6 | Cones, signs, birds and ENGLAND train as independent configurable plugins; runtime toggle and complete cleanup. |
| M7 | Opponents, original menus, full CUP and all eight maps. |
| M8 | Profile and optimize CPU/GPU/query/upload/draw work; full-content regression and standalone package. |

Each increment must leave a runnable build. Small changes get focused checks;
large changes get broader checks. Prefer branches and short sequences. Finish
one usable slice before expanding it; preserve the runnable driving and static views.
Stage exits are fixed. Record new dependencies under their owning later stage;
the full objective stays unchanged. Ready kernels are not integrated features.

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
