# Rust rewrite roadmap

## Current checkpoint

- Active: **M3 — physics**, beginning with terrain-query data/arithmetic.
- C reference committed; source-byte preservation fixed (`052ba81`).
- M1 implementation commit: `38841c4`.
- M2 geometry (`50b4d38`): direct BSM decoder matches the complete C loader on all eight
  maps, 80,439 triangles (vertices/origins/normals/colors/indices/UV/flags/materials).
  Native inspector: `cargo run -p tankracer-assets --bin inspect-world --
  assets/game/LEVELS/ENGLAND/FASTFILE.DAT`. Details: `rust-assets.md`.
- M2 SUR (`e86657a`): 1,007 images/mips, 5,931,008 pixels exactly match C; one invalid BIO
  resource explicitly rejected. Build/run the optional oracle per `rust-assets.md`.
- M2 scene verified: actual ENGLAND static BSM content, 56 materials/one atlas,
  5 draws/8,762 visible triangles, 542 GPU billboard triangles; free camera.
  11 native checks, 2 optional C oracles and 133 GPU pixel cases pass. Inspector
  uploads 20,508 index bytes on camera change, zero when stationary.
- Per-map retained acceleration/metadata currently **376,808 bytes in Wasm**;
  physics must share the remaining budget. GPU asset storage is separately
  listed in `rust-renderer.md`. Actors/sky/layers are subsequent milestones.
- Next: inspect `src/physics_terrain.c`; decode its existing 8x8/node/triangle
  graph into typed Rust, preserving candidate order and wrapping arithmetic;
  add C query oracles/boundary tests before any query acceleration.
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
concrete task. Then proceed to the next small, runnable increment.
