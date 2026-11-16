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

- **M1-M3 and M4.1-M4.2 accepted; verified code: `f7b9dc8`.** The Rust/Wasm
  driving scene draws authored body, turret, gun and both track/wheel meshes
  from read-only physical part poses. Fixed-tick visual travel supplies GPU UV
  phases; animation uploads no vertices and cannot mutate physics/RNG.
  See [M4 evidence](rust-m4-vehicle.md).
  M2 remains the ENGLAND static scope; [M3 acceptance](rust-m3-acceptance.md)
  records the original moving-proxy qualification (baseline `686cb74`).
  Arrows drive; A handbrakes; C switches follow/free camera; `/?inspect` stays static.
- **Focused M4.2 checks passed:** eight scene tests, including 15 cars/four
  articulated poses; original x86 track oracle: 90 cases / 11,880 GPU corners;
  native/Wasm lints; browser 170 GPU cases, direct/key/brake native/Wasm final
  fields and ten scenery probes. Draw-every-tick versus draw-at-end sequences
  preserve animation/physics/RNG; handbrake freezes, reload resets, redraws and
  animation upload zero vertices. Production build and source preservation pass;
  game/physics source is unchanged. Unrelated C suites were not repeated.
  Production Wasm: **721,163 B**.
- **Next runnable increment: M4.3.** Original camera modes, ground/body shadows
  and additional vehicle layers/attachments, one usable slice at a time. Keep
  the current driving scene and physical updates unchanged.
- **Later owners:** M5 full world/Player startup, allocation/RNG order, race
  lifecycle, laps, pickups/weapons/HUD/effects; M6 configurable scenery; M7
  opponents, original menus/audio/full CUP/all maps; M8 full optimization/package.
  A complete Rust race, per-frame browser/C parity and full image parity remain
  unqualified. Qualified component kernels are not integrated game features.
- **Measured baseline:** default vehicle 105 triangles / 9 draws; ENGLAND scene
  404 draws / 10,126 triangles / 25 pipeline changes. Vehicle render caches:
  35,098 B, separately counted; world vertex layout remains 44 B. Extra resident
  map data: ENGLAND **769,325 B** (785,669 B shattering peak), SWAMP **880,874 B**,
  BIO **950,660 B**; all below 1 MB. Other maps' full live aggregates remain
  unqualified; transparent batching/culling remain pending. Keep original
  resources, GPU and dynamic game memory separate.
- Rust output: `build/rust/site`; preserve the C/browser reference. Local
  increment commits are authorized; **do not push** unless asked.

## Incremental delivery

| Stage | Deliverable / exit condition |
| --- | --- |
| M1 done | Rust/Wasm build, browser shell, own raw WebGL renderer, explicit state and GPU tests. |
| M2 done | ENGLAND static scene, categorized resources/materials, atlas/batches and free camera. |
| M3 done | Independent moving proxy: precise terrain, suspension, drive, poses and collision; C module/short-sequence parity. [Acceptance](rust-m3-acceptance.md). |
| M4 active; M4.1-M4.2 done | Body/turret/gun and animated tracks/wheels accepted. Next cameras, shadows and material layers. |
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
