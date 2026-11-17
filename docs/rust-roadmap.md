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

- **M1-M4 accepted within their fixed scopes; pre-M5 structure cleanup accepted.**
  Accepted foundation: `7c93bbc` (review fixes: `a150546`). Rust/Wasm still delivers
  the existing driving scene,
  not a complete race. Authored vehicle parts/tracks, shadows, original cameras,
  instanced aerial and fused BLOB layer remain accepted; see
  [M4 evidence](rust-m4-vehicle.md). M2 remains ENGLAND static scope;
  [M3 acceptance](rust-m3-acceptance.md) retains its original moving-proxy boundary.
- **Ownership is now shared:** native/browser use one existing session update
  order and resource preparation; inspection cannot publish simulation cameras.
  Configuration/terrain/surfaces are read-only; mesh caches retain instance
  identity as well as revisions. Generic mesh preparation has no vehicle rules;
  materials/GPU cache reuse have shared owners.
  [Structure contracts/evidence](rust-structure.md) records the boundaries.
  No countdown, pause, plugin lifecycle or later-stage feature was added.
  Arrows drive; A handbrakes; C cycles views; V holds rear view; F toggles free
  inspection; `/?inspect` stays static. A keeps its keydown ownership across F.
- **Retained foundation verification:** four pre-refactor native checkpoints; 96 manual-order
  comparisons; 24 running-inspection updates with identical simulation state and
  an active BIO Mover. C references cover 2,880 vehicle / 348 scenery updates,
  60 vehicle initializations, 192 camera updates and 2,772 layer/aerial fields;
  original x86 tracks cover 90 cases / 11,880 GPU corners. See specialist docs
  for these accepted suites; current increment results are below. Use the
  selective runner for external references.
- **M5.1a Player route startup accepted.** Shared native/browser loading binds
  the real Player after route linkage. Complete C startup agrees on all eight
  default selections / 72 semantic fields; browser/native route state agrees.
  The added query leaves physics, scheduler, RNG and all four saved native
  checkpoints unchanged. No checkpoint advancement or countdown was added.
  [M5 plan/evidence](rust-m5-race.md) fixes the next slices and records limits.
- **M5.1b ordered construction accepted at `8612f7c`.**
  Standalone Rust `startup` constructs every authored record in order, links
  routes/backgrounds, drains startup queues, and owns the fallback Player,
  starting camera and finish entity. All 22 usable CUP files use the fallback;
  other authored Player/AI inputs remain explicit unsupported classes. Real
  Flock/Train/emitter/lap/camera initial state supplies the RNG consumption;
  there are no RNG skips or identity padding. Full seeds now match C on all maps.
  C checks pass for **25 selections / 4,743 constructors / 111,065 fields**,
  including per-record RNG, actor state, IDs, poses and startup list order.
  Native/Wasm construction agrees on ENGLAND/SWAMP/BIO; CIRCUIT's unused cups
  fail explicitly. Workspace **111 passed / 67 external ignored**; selected
  C startup/scenery/model references, old four native checkpoints, browser/191
  GPU cases, lints and production build pass. Production Wasm **818,231 B**;
  no test probes leak. Original C/host/browser/assets are unchanged.
  Construction is a read-only cold boundary, not a complete race tick. Actor
  ticks fail explicitly until M6; accepted browser driving keeps its previous
  subset/seed and unchanged physics/update order. No map precomputation added.
- **M5.1c countdown/clock accepted at `b09dabb`.**
  Normal browser startup now uses the original 3000 ms countdown, launch gate
  and race timer through Session/Player owners. Clock precedes the existing
  update sequence; physics alone clamps raw elapsed to 10..100 ms. Release
  preserves original frame/overshoot boundaries and fast-start input rules.
  C/native: 12 sequences / 144 updates / 941,304 fields; native/Wasm: 12 scene
  updates. Existing 2,880 vehicle updates / 18,041,268 fields, four saved native
  checkpoints and browser/191 GPU cases pass. Workspace 113 passed / 68 external
  ignored; lints/production pass. Wasm 819,845 B; no debug exports. Original C
  production sources unchanged. No full cold-world ticks or lap/finish claims.
- **Next: M5.2 interaction slices, also authorized by the user in this run.**
  Integrate actual player actions and their visible consumers, then pickup ->
  use -> hit -> cleanup. Preserve physical kernels/update positions and RNG;
  qualify branches and short sequences. M5.1d retains progression/finish/restart/
  exit ownership; implement real dependencies before claiming complete M5.2.
  Do not tick the cold world through unsupported optional actors or fake their
  updates. Complete-race and full-frame parity remain open.
- **Later owners:** M5.2 pickups/weapons/damage/effects/HUD, forced effect material
  styles, starting-camera/environment shifts and camera impulses; M6 configurable
  scenery; M7
  opponents, original menus/audio/full CUP/all maps; M8 full optimization/package.
  A complete Rust race, per-frame browser/C parity and full image parity remain
  unqualified. Qualified component kernels are not integrated game features.
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
