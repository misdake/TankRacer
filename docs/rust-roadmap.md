# Rust rewrite roadmap

## Current checkpoint

- Active: **M3 — physics**. Verified modules and model construction exist;
  the complete moving Rust vehicle is **not running**.
- Completed: M1 build/render shell (`38841c4`); M2 ENGLAND static world/atlas/free
  camera (`e98e429`): 5 draws, 8,762 visible triangles; 133 GPU pixel cases pass.
- M3 verified against C: terrain/contact/suspension, drive/fixed-point math,
  vehicle poses/model assembly/damage, ordered object queries and collision
  response, deformable objects, breakable hit chains, spatial/tick membership,
  impact particles/shared line pool and real synchronous ground-effect bindings.
- Last verified code: **`1d45e70`**. 4,480 pool/particle/composed ground records
  match C, including all 15 real vehicle bodies. Existing 9,600 damage records,
  workspace tests, native/Wasm Clippy and unchanged-C source verification pass.
- Remaining scene gaps include tank/obstacle particle bindings,
  auxiliary/destruction effects, object motion ticks and the full scheduler;
  probes do not complete them.
  Rust world construction and the complete moving vehicle are still unfinished.
- Detailed contracts, counts and reproduction commands live in [assets](rust-assets.md),
  [physics](rust-physics.md) and [renderer](rust-renderer.md); keep them out of this summary.
- Budget: render acceleration/metadata **376,808 bytes in Wasm**. Complete
  collision graphs cost 175–330 KB/map native and share the same map limit;
  not yet in the live Wasm counter. GPU resource bytes are counted separately.
- Next: vehicle motion and complete per-tick vehicle orchestration, including
  remaining real collision-effect bindings → browser proxy.
  No permanent original-memory arena or C runtime dependency.
  M3 stays open until the proxy runs.
- The existing C/browser game remains the working reference. Rust output goes
  to `build/rust/site`; no automatic push or replacement of the reference site.

## Milestones

- [x] **M1 Foundation:** Rust build, Wasm/browser shell, explicit render state,
  indexed geometry, resource loading foundation, GPU correctness tests.
- [x] **M2 Static world:** ENGLAND meshes/materials, resource provenance, free
  camera, atlas/texture classification and static batches.
- [ ] **M3 Physics:** simple vehicle proxy; terrain → contact/suspension → drive
  and pose → collision. Compare each module and short sequence against C.
- [ ] **M4 Vehicle:** body/turret/tracks/wheels, camera, shadows and material layers.
- [ ] **M5 Core race:** pickups, weapons, destruction, laps, timing and HUD.
- [ ] **M6 Optional content:** cones, signs, birds and ENGLAND train, independently
  removable with tested runtime enable/disable and complete cleanup.
- [ ] **M7 Complete game:** opponents, original menus, full CUP and all eight maps.
- [ ] **M8 Optimize/release:** measured CPU/GPU/query/upload/draw improvements,
  full-content regression and standalone static package.

Every step leaves a runnable version. Commit milestones after relevant checks;
use short branch/sequence tests, extending coverage when changes justify it.

## Non-negotiable constraints

1. Final runtime is clean, readable **Rust → Wasm → own raw WebGL renderer**.
   C is a migration/test oracle, not a permanent address-memory runtime.
2. Immutable resources, authoritative simulation and read-only render scene are
   separate. GPU does world transforms/projection/clipping; static meshes stay
   resident. Preserve exact physics arithmetic, update/event order and RNG.
3. Minor visual differences are acceptable; wrong blend, depth, masking or
   texture edges are not. Material/pipeline states are explicit and GPU-tested.
4. Optional modules own resources, entities, colliders, effects, sounds and
   subscriptions. `enabled=false` removes the feature; `visible=false` only
   hides it. Config changes apply at fixed ticks and are recorded for replay.
   Audit shared RNG/order dependencies; batching must preserve detachability.
5. Additional per-map acceleration: aim **<1,000,000 bytes**, absolute maximum
   **2,000,000 bytes**, decimal, resident/uncompressed including metadata,
   alignment/capacity. Above 1 MB needs convincing whole-frame measurements.
   Account original resources and transient/dynamic memory separately.
6. First principles: eliminate unused/repeated work, precompute immutable work,
   improve layout, batch/share work, then choose caches/spatial structures from
   profiles. Preserve query priority/boundaries; do not approximate collision
   height, normals or surface material. Report gains and costs honestly.

## Checkpoint procedure

Read this file before each milestone, after completing one, and after context
recovery. Update the checkbox, current checkpoint, verification/commit and next
concrete task. Check the worktree before resuming; distinguish verified work,
pending changes and remaining scope. Keep this summary short and move detailed
evidence into the linked documents. Then proceed to the next runnable increment.

At each checkpoint, answer: **What runs now? What passed? What is still missing?
What is the next runnable increment?** Recheck the constraints above before
choosing that increment. A tested helper alone does not complete a milestone.
