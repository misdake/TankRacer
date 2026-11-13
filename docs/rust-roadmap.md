# Rust rewrite roadmap

This is the short source of truth. Read it before/after each milestone and after
context recovery; check the actual worktree before resuming. Detailed evidence:
[assets](rust-assets.md), [physics](rust-physics.md), [renderer](rust-renderer.md).

## Current checkpoint

- **M3 active; M1/M2 complete.** Rust currently runs the ENGLAND static world and
  free camera: 5 draws, 8,762 visible triangles; 133 GPU pixel cases verified.
  The complete moving Rust vehicle is **not running**.
- Last verified code: **`e66ecfd`**. Live scheduler/deferred queues (585 scenarios),
  name interning (224 operations) and named collision attachments (4,096 records,
  441 binding changes) match C boundaries. Existing membership, workspace tests,
  native/Wasm lint, unchanged-C checks and release browser build pass. Earlier
  resources/parent construction/connected physics remain qualified; details in
  [physics evidence](rust-physics.md).
- Still missing: canonical entity allocation/ID reuse, concrete subclass methods,
  authored attachment/factory binding, complete startup and shared RNG replay,
  vehicle timer/ability prefix and after-pose effects. Scheduler tests supply
  scripted callbacks; constructors still use some C boundary inputs. Neither
  proves a running world. New fragment/animation rendering is pending.
- **Next runnable increment:** canonical entities and concrete subclass lifecycle/
  method bindings, authored references and complete world membership/ticks;
  connect vehicle prefix/after-pose and scheduler -> independent moving proxy.
  M3 stays open until it runs without C or test snapshots. Preserve shared mesh
  ownership, RNG consumption and the distinction between activation/impact limits.
  Interning a forward name does not allocate an entity ID in the original.
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
large changes get broader checks. Prefer branches and short sequences; extend
sequence length when a delayed failure or an actual dependency justifies it.
Split each stage into small usable slices; finish and verify one slice before
expanding it. Keep the existing static browser runnable while M3 is incomplete.

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
2. Record **what runs, what passed, what is missing, and the next runnable step**.
3. Put detailed contracts/test counts/commands in the linked specialist docs.
4. Commit verified work, update this checkpoint with the code commit, and re-read
   it before continuing. Keep pending changes distinct; a tested helper alone
   never completes a stage.
