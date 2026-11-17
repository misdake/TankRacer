# Rust delivery plan

Updated 2026-11-17. The [roadmap](rust-roadmap.md) holds the one current checkpoint.
This file fixes stage boundaries and deliverables; specialist docs hold evidence.
The C game remains the complete playable reference. Rust currently delivers a
drivable proxy, not a complete race. No overall completion percentage is claimed.

## Acceptance and scope

Every increment leaves a runnable build and one local code commit. Small changes
get focused checks; shared ownership/render integration changes get broader
regression. A stage closes against its listed exit, not every remaining game
dependency. Original arithmetic, RNG and update order remain required across
the full project. Moving unfinished work to its owning stage does not remove it.

| Stage | Status | Concrete exit |
| --- | --- | --- |
| M1 build/renderer | Complete | Independent Rust/Wasm/browser build, raw WebGL renderer and explicit GPU state tests. |
| M2 static world | Complete for its ENGLAND scope | Typed world/material resources, atlas/batches, free camera and runnable static inspection. Full all-map rendering is M7. |
| M3 moving physics | Complete; [accepted](rust-m3-acceptance.md) | Resource-built native/Wasm proxy drives on real terrain, input/contact/suspension/drive/pose/collision runs through shared owners, related C short-sequence tests and browser regression pass. |
| M4 vehicles/view | Complete for the existing driving scene; [evidence](rust-m4-vehicle.md) | Real vehicle geometry, articulation, tracks/wheels, original cameras, shadows and material layers work in the existing driving scene. |
| M5 single-player race | In progress; Player route startup qualified, full race pending | An opponent-free race starts, accepts inputs, supports pickups/weapons/destruction/HUD, counts laps, finishes and can restart/exit through real owners. |
| M6 optional scenery | Pending; some actor foundations ready | Configurable cones/signs/birds/train and other minor content can enable/disable at fixed ticks with complete ownership cleanup and documented RNG/order effects. |
| M7 complete CUP | Pending | Multi-car runtime, AI, original CUP menus, audio, selection and all CUP transitions/maps run end to end. |
| M8 performance/release | Pending; initial atlas/cache work done | Whole-frame CPU/GPU profiles, justified query/upload/draw improvements, all-content regression and standalone static package. |

## M3 closure checklist

The original definition is preserved: terrain -> contact/suspension -> drive/pose
-> collision, shown with a simple vehicle proxy and checked against C. Existing
shared effects needed by this chain were ported to preserve state/RNG; that does
not require their complete rendered appearance before M3 can close.

- [x] Immutable resource loading and fresh vehicle construction, without a C
  runtime, memory dump or emulator in the Rust executable/browser.
- [x] Terrain, height/normal/material, suspension, powertrain, attitude/poses,
  collision and motion connected through real owners.
- [x] Keyboard input/reverse selection, handbrake/restoration and collision
  indication connected to fixed updates.
- [x] Obstacle/Crush/Fall/Bounce/Closer/Spin/Mover scenery and relevant destruction
  state connected; proxy remains runnable in the browser.
- [x] Native C module/short-sequence qualification; native/Wasm and GPU checks
  passed at committed checkpoint `686cb74`.
- [x] Finish the WayPoint increment: review, native boundary checks, browser
  regression and memory accounting passed. It is advance M5 bootstrap.
- [x] Commit the verified increment (`686cb74`), record current evidence and
  close M3 against its original exit condition.

Closing M3 does not certify full original Player tick, complete world allocation/
RNG startup, route/lap controllers, full fragment/explosion rendering or a whole
race. These remain explicit M5-M7 work. No new subsystem is added to M3 closure.

## Fixed delivery order after M3

| Increment | Work and acceptance |
| --- | --- |
| M4.1 body/turret/gun — accepted | Real meshes/materials replace the box using read-only physical part poses. Transform, geometry refresh, browser/GPU and physics-preservation checks pass at `1e10aa9`; [evidence](rust-m4-vehicle.md). |
| M4.2 tracks/wheels — accepted | Authored track poses and wheel/tread UV animation work at `f7b9dc8`; original x86 UV/travel, browser/GPU and draw-cadence checks pass with unchanged physics and zero animation vertex uploads. [Evidence](rust-m4-vehicle.md). |
| M4.3 camera/shadows/layers — accepted | Original cameras/inputs, ground/body shading, aerial and BLOB normal layer work in the driving scene; each view and relevant blend/depth transitions are checked. |
| M4.3a shadows/attachments — accepted | Ground projection, whole-vehicle shade and normal Under/End parts at `0a54f31`; geometry/GPU/framebuffer isolation and unchanged-physics checks pass. [Evidence](rust-m4-vehicle.md). |
| M4.3b original cameras — accepted | Actual near/far/cockpit chase modes, C/V input, Bottom/XHAIR and next-update feedback now run in the browser. C short sequences, native/Wasm ordering, GPU tests and read-only redraws pass; [evidence](rust-m4-vehicle.md). |
| M4.3c aerial/layer — accepted | Four authored aerial segments are instanced; BLOB uses original gates, normals, integer UV projection and additive RGB fused with its base. C, GPU, six-view isolation and unchanged-update checks pass. This meets the unchanged M4 driving-scene exit. |
| Pre-M5 structure — accepted | Shared native/browser session and loading, read-only inspection, immutable configuration, mesh revisions, generic attributes/materials and shared GPU cache; selective reference runner. [Contracts/evidence](rust-structure.md). No later-stage behavior added. |
| M5.1a Player route startup — accepted | Shared loading binds the actual Player to its initial checkpoint after route linkage. Eight complete C startups / 72 semantic fields and native/Wasm route checks pass; full startup RNG/identity remain open. [Evidence](rust-m5-race.md). |
| M5.1 race ownership | Finish required world/Player startup, route/checkpoint/lap progression, countdown, timing, finish, restart and exit. Audit startup allocation/shared RNG and short complete updates against C. |
| M5.2 race interactions | Integrate pickup activation/backup/restore, weapon/projectile variants, damage and retirement; draw actual effects and HUD. Exercise pickup -> use -> hit -> cleanup through actual owners. |
| M6.1 module boundary | One configuration path and tick-boundary changes; modules own entities, colliders, resources, effects/audio and subscriptions. Test one module's full disable/re-enable first. |
| M6.2 optional content | Migrate remaining minor actors to that boundary, retaining original enabled behavior and explicit disabled behavior. Complete cleanup is required. |
| M7.1 opponents | Extend the runtime from one vehicle to actual peer owners, then integrate AI route/avoidance/weapon decisions and qualify coupled updates. Existing tank-collision kernels alone do not complete this. |
| M7.2 complete presentation/CUP | Original menu logic/rendering, actual key bindings, audio, all cars/maps and full CUP result/selection/reload transitions. |
| M8 profile/package | Optimize measured whole-frame costs, especially repeated spatial work and transparent draw runs; produce final all-map budget and release evidence. |

## Existing work that must not be counted twice

- Exterior parts/animated tracks/wheels and ground/body shadows render from
  canonical state (M4 accepted), including original near/far/cockpit modes,
  look-back, aerial and BLOB normal layers. Full countdown/
  boost/world startup side effects belong to M5. Original turret-key/
  fire gameplay integration is not supplied by displaying articulated poses.
- Explosion/smoke/debris, lights/marks and other after-pose updates have qualified
  components; their full geometry/material presentation is not delivered.
- Some pickup fade/shield/temporary-effect kernels exist; full authored pickup
  creation/activation and complete interactions remain M5.
- WayPoint construction/linkage/grid/weather and queries are accepted bootstrap
  work; the real Player now owns its initial checkpoint (M5.1a). Checkpoint
  advancement, lap/progress controllers and their events remain M5.
- Actor readers and lifecycle foundations exist; they do not yet provide the
  required configuration/runtime plugin lifecycle.
- GPU blend/depth tests, atlasing and duplicate-cache removal exist; full scene
  lighting, transparent batching/culling and measured whole-game optimization
  are unfinished.

## Budgets and checkpoints

Extra map preprocessing stays below 1,000,000 B preferably and never exceeds
2,000,000 B resident/uncompressed, including capacities/alignment/metadata.
Above the preferred target requires measured whole-frame gains. Account original
resources and transient/dynamic simulation separately. Every increment reports
affected maps; unmeasured maps remain unqualified until their acceptance.

At each completed increment, re-read this file and the roadmap, update only the
current checkpoint, record remaining work under its owning stage, and commit.
Do not turn module test counts into an overall completion percentage. Do not
push unless requested.
