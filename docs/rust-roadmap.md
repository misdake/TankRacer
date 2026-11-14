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

- **M3 active; M1/M2 complete.** Browser runs the ENGLAND static world and free
  camera: 5 draws, 8,762 visible triangles; 133 GPU pixel cases verified.
  Moving Rust vehicles are not yet connected to the browser.
- Last verified code: **`2ca8a24`**. Fresh players use the shared tagged entity
  table/factory, class registry, drawing lists and tick activation: 122 / 84,668
  words match C, including flags/memberships. Eight affected object suites and
  workspace/native/Wasm/build/preservation checks pass. Connected full physics
  (1,254 updates / 17,591,969 words) is also qualified.
- **Uncommitted increment:** `vehicle_runtime.rs` and the `drive` example now
  initialize from resources and run prefix/physics/after-pose through shared ticks.
  ENGLAND/car 1 moves for 120 updates, grounded for 117, without C or snapshots.
  This smoke test does not qualify the new startup/update wiring against C.
  Other maps, collision radius, lifecycle and full startup RNG remain unverified.
- **Next runnable increment:** qualify this fresh native path against C in short
  sequences, then connect a moving browser proxy. Keep M3 open until its browser
  exit condition runs independently. Reuse owned dynamics without per-frame
  clones; bring other readers forward only when needed for this slice.
- Pending beyond it: vehicle blast response, Bounce/Spin/remaining readers,
  full startup/shared RNG replay, pickup activation/projection/backup/restore,
  fragment drawing and long-name/header aliases. Keep these explicit through M5-M7.
- Extra map memory so far: render metadata **376,808 B in Wasm**; native collision
  graphs 175-330 KB/map are not yet in the live Wasm total. Aggregate them before
  claiming budget compliance. Original resources/GPU/dynamic memory are separate.
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
