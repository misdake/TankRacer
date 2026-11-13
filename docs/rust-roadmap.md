# Rust rewrite roadmap

## Current checkpoint

- Active: **M1 — build and renderer foundation**.
- C reference committed; source-byte preservation fixed (`052ba81`).
- Rust/Wasm target installed, workspace and dependencies prepared.
- Next: finish world-space indexed WebGL renderer; build Wasm; pass GPU
  blend/depth/state-transition tests; document runnable commands; commit M1.
- The existing C/browser game remains the working reference. Rust output goes
  to `build/rust/site`; no automatic push or replacement of the reference site.

## Milestones

- [ ] **M1 Foundation:** Rust build, Wasm/browser shell, explicit render state,
  indexed geometry, resource loading foundation, GPU correctness tests.
- [ ] **M2 Static world:** ENGLAND meshes/materials, resource provenance, free
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
concrete task. Then proceed to the next small, runnable increment.
