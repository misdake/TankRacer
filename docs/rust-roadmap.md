# Rust rewrite roadmap

This is the short source of truth. Read it before/after each milestone and after
context recovery; check the actual worktree before resuming. Detailed evidence:
[assets](rust-assets.md), [physics](rust-physics.md), [renderer](rust-renderer.md).

## Goal

Incrementally replace the C runtime with readable Rust, compiled to Wasm, and a
small raw WebGL renderer with ordinary world-space meshes/materials. Deliver a
standalone single-player CUP game, detachable scenery plugins and a measured,
optimized package. Keep a runnable build at every step; qualify helpers only
as needed to reach the next playable slice.

## Current checkpoint

- **M3 active; M1/M2 complete.** Browser now drives a fresh Rust/Wasm vehicle on
  ENGLAND, displayed as a simple proxy: 6 draws / 8,774 triangles. Arrows drive;
  C toggles follow/free camera. `/?inspect` keeps the static inspection view.
- Last verified code: **`3b54c15`**. Native/browser share resource loading and
  tick runtime; no C or snapshots at runtime. VR01 still passes 38 scenarios /
  1,056 updates / 6,581,504 fields against C across all maps/cars. Browser input
  works; a fresh 120-update run matches Native final position/velocity/RNG.
  `npm run test:rust` passes, including 133 GPU cases; production build and C
  preservation pass. Production Wasm: **485,537 B**. Details are in linked docs.
- **Next runnable increment:** bind authored scenery collision objects to this
  live browser/runtime path before closing M3. Audit supported versus missing
  level classes and their startup dependencies; preserve factory order/RNG.
  Vehicle lifecycle, full Player/race startup and shared-world RNG are still
  incomplete. Bring readers forward only when required for the runnable slice;
  borrow owned dynamics without per-frame clones. Real vehicles/cameras are M4.
- Pending beyond it: vehicle blast response, Bounce/Spin/remaining readers,
  full startup/shared RNG replay, pickup activation/projection/backup/restore,
  fragment drawing and long-name/header aliases. Keep these explicit through M5-M7.
- Combined live ENGLAND extra map memory: **638,906 B in Wasm** = 377,176 B
  render/browser metadata + 261,730 B collision data/Arc. One terrain graph is
  shared. Other maps still need combined live accounting when enabled.
  Original resources/GPU storage/dynamic game memory remain separate.
- Preserve the working C/browser reference. Rust output: `build/rust/site`.
  Local milestone commits are authorized; **do not push** unless asked.

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
