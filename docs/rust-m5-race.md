# M5 single-player race

Updated 2026-11-17. M5.1a route binding, M5.1b ordered construction and M5.1c
countdown/clock are qualified within the boundaries below. The browser now runs
the clock on the accepted driving scene; full-game frame parity remains open.

## Delivery order

| Slice | Required result |
| --- | --- |
| M5.1a route startup | Real Player owns its starting checkpoint after authored routes are linked; native/browser share loading. |
| M5.1b ordered startup | Real authored constructors, fallback Player, starting camera and finish-owner identities, initialized state and shared RNG; standalone native construction and Wasm check. |
| M5.1c race clock | Original countdown/release/driving gates and elapsed time at their real update positions, with short C/native/Wasm boundary sequences. |
| M5.1d progression/lifecycle | Checkpoint/lap records, object restoration, timing, finish, restart and exit through actual owners. |
| M5.2 interactions | Pickups, weapons, damage/destruction, visible effects and HUD through the same race owner. |

Optional scenery behavior/configuration stays M6; opponents, complete CUP menus,
audio and all-map presentation stay M7. Construction dependencies do not move
those runtime features into M5.1b.

## Ordered construction

`world_startup::Constructed` is the complete **cold construction** owner. It uses
the existing resource loader, scene factory, shared RNG, scheduler and vehicle
assembly. It constructs the authored records in file order, links routes and
background targets, drains startup queues, creates the fallback Player and
starting camera, binds Player's route, then creates the finish owner. Both
session owners use real factory allocation/initialization methods.

The 22 usable CUP level files contain no authored Player: their actual path is
the fallback. An authored Player/AITank in another input is an explicit unsupported
class, not silently omitted. CIRCUIT bronze/silver have no usable route and are
rejected; the original C startup rejects those selections too.

The actor store owns actual initial data for Noisy, Smoker, Trackcam, Bomber,
Train, Flock, Powerup, BckgrdOb and Lamps. Random values initialize real bird
positions/bases/speeds, emitter timers, yacht rates, propeller timers,
random-lap visibility and the starting camera variant. Existing deformation
constructors see the resulting RNG in its correct position. There are no RNG
skip/advance operations or identity-padding entities.

The current original C consumption counts and full-construction Rust counts are
identical for the eight default selections:

| Map | C and Rust LCG draws from seed zero | Authored records |
| --- | ---: | ---: |
| ENGLAND | 1714 | 240 |
| MEXICO | 2044 | 244 |
| SWAMP | 1773 | 218 |
| EAST | 874 | 147 |
| THEME | 1738 | 177 |
| BIO | 1204 | 201 |
| MONACO | 1504 | 131 |
| CIRCUIT | 1513 | 82 |

Equal final seeds alone are insufficient. The constructor oracle compares RNG
before/after **every record**, identity order, final object poses, selected
initialized actor fields, and all startup queue/list orders. It also checks the
starting camera, finish-owner bounds/configuration, saved steering limit and
shared pickup geometry. Address-bearing fields use logical entity IDs, model
presence or vertex indices rather than comparing host allocations.

Source quirks kept deliberately:

- EAST's final undelimited `:fall_range` token leaves the native integer reader's
  previous value intact; the original then scales that existing range again.
- Train's explicit cue range overrides its expanded path range last. Ferris
  constructs its own basis; Eagle retains the source decimal constant and
  rejects ambiguous x87 truncation boundaries.
- Pickup resource/type availability depends on CUP level. An authored shape
  whose type was not installed retains type zero.
- Trackcam startup stops/prepends cameras; Powerup has a distinct post-construction
  method; BIO background linkage can enter its target/remove its lap membership;
  the delayed lander retains its own custom-render membership.

Pure resource decoding and model-derived arithmetic are eager: their temporary
allocation/I/O order is not a claim of original address or byte-for-byte RAM
layout. These calculations perform no RNG draws or game entity allocation.
There is no new map acceleration/precomputation in this increment. Original
resources and constructed actor state remain separate from the map query budget.

## Runnable boundary and next work

The independent native entry stops at the constructed-world boundary:

```sh
cargo run -p tankracer-game --example startup --release -- assets/game 0 1 0
```

Arguments: asset directory, map index, vehicle ID, optional CUP index. Its JSON
array contains RNG, owner IDs, constructor/actor counts, initial camera, route,
scheduler counts and frame zero. It uses Rust and resource archives only.

`Constructed` exposes a read-only world, not a frame update API. Optional actors
have explicit unsupported tick bindings, never successful no-op update methods.
Their configuration/ticks/drawing/cleanup remain M6. The normal native/browser
`Session` still owns the accepted immediate-driving subset and its old RNG;
it is intentionally not switched to a complete world with unsupported ticks.
The full constructor is also exercised in Wasm by a test-feature-only probe.
Production has no construction/debug probe export.

M5.1d owns progression/finish/restart/exit; M5.2/M6 must supply real runtime owners
before complete-world updates can claim parity. Do not use the driving subset's
seed as full-startup state.

## Countdown and race clock (M5.1c)

`race_clock::Clock` owns countdown/release/display; each actual Player owns its
`Racer` lap count, elapsed time and driving gate. `Session::begin_race` requires a
fresh world, initializes the original 3000 ms countdown and disables driving.
The normal page calls it after loading; the independent `drive --race` example
exercises twelve coarse boundary updates. Immediate-driving fixtures still use
the same Session without a clock, preserving the accepted qualification API.

On each race update, raw elapsed time reaches the clock **before** Player input,
the Tank prefix and physics. The rest of the accepted camera/world/animation/
feedback sequence is unchanged. Original camera starting-view convergence is
independent of the countdown; no countdown-derived camera gate was invented.

- Input stages switch at 1500/1000 ms, separately from the visual 3/2/1 digits.
- Crossing zero enables driving and starts a fresh 1000 ms GO interval. It does
  not add this update to elapsed lap time or carry countdown overshoot forward.
- Later updates decrement GO and add raw elapsed time to each present racer.
  The original wrapping arithmetic is retained.
- Physics independently clamps elapsed time to 10..100 ms. A 1000 ms delayed
  update advances race time by 1000 ms and physics once by 100 ms; it is not ten
  physics steps. The browser's fixed updates remain 16 ms.
- Existing Player input implements early-hold/timed-throttle launch eligibility;
  its existing countdown RPM prefix still runs while driving is disabled.
- `begin_race` rejects an already-started world. Restart/exit must construct or
  restore the actual owners under M5.1d, not reset only the visible clock.

This connects the clock and gates to a runnable scene; it does not execute the
complete cold actor set or add lap crossing, pause, HUD, weapons or finish logic.
The user also authorized M5.2 in this run. Next integrate actual interactions in
small runnable slices, retaining M5.1d as owner of progression/lifecycle.

M5.1c verification: **12 C/native short sequences / 144 updates / 941,304 fields**
cover timely, early and late throttle, exact release, overshoot, and large raw
elapsed values. The oracle calls actual `frame_race_*`, `frame_time_physics_dt`,
Player input and the native prefix/physics/after-pose path; it isolates that
chain from unimplemented surrounding race behavior. Twelve native/Wasm scene
updates agree including clock, controls, RPM, pose, velocity and RNG. Existing
2,880 vehicle updates / 18,041,268 fields and four saved native checkpoints
still agree. Workspace **113 passed / 68 external ignored**, 191 GPU cases,
native/Wasm lints and production build pass. Production Wasm **819,845 B**, with
no race/debug probe export. Original production C/host sources are unchanged.

```sh
cargo run -p tankracer-game --example drive --release -- assets/game 0 1 12 --scenery --race
node tools/test_rust.mjs reference race-clock vehicle-runtime
```

Evidence: `build/rust/tests/m5c-*` and the browser report. The test snapshot codec
now serializes effect records through a private copy, keeping inspection read-only.

## Verification

### M5.2a HUD presentation

The normal browser now draws the original CFONT1/2/3, dial, map and GO resources.
`scene::hud` translates the original anchors, glyph advances, one-pixel font V
offset, quarter-pixel needle rotation, minimap calibration, countdown scale,
GO spacing, timer blink and speed arithmetic. The cockpit shows the original
turret indicator. This is a read-only presentation of the current clock/vehicle;
lap progression, lap records, inventory icons, warning events and standings
changes require their M5.1d/M5.2 owners and are not fabricated here. The current
opponent-free scene is still at lap one/rank one.

One HUD atlas supplies ordered opaque -> alpha128 -> opaque runs: three draws
during countdown/GO and one otherwise. Depth test/write are off for every HUD
draw; keyed runs discard zero coverage, blended runs use premultiplied alpha.
Static redraws reuse geometry; render/inspection never advances clock or RNG.
The atlas contains original resources, not extra per-map query/precompute data;
decoded pixel buffers are released after upload. The Rust build includes COMMON
for the actual font/GO resources. Returning to inspection hides the HUD.

`reference_hud.c` calls unchanged `frame_hud_draw` and compares **128 snapshots /
88,384 fields** across eight maps: every quad's position, UV, color, order,
depth/write/key/blend state, including cockpit indicators and boundary/blink
times. The browser verifies visible overlay/countdown pixels over the 3D scene,
three ordered atlas draws, zero stationary geometry uploads and unchanged game
state/time. Workspace **114 passed / 69 external ignored**, lints, production and
the existing 191 GPU cases pass. Production Wasm: **836,272 B**. Logs:
`build/rust/tests/m5-2-hud-*`; commands include `reference hud`, `unit`, `lint`,
`web` and `node tools/build_rust.mjs`.

### M5.2b turret actions

Z/X now feed `turret::State` through Player's actual input position, after its
collision indication update and before auxiliary braking/Tank dispatch. Original
acceleration, damping, both-button recenter latch, cooldown, centering lock,
signed angle wrapping and crossing-zero stop are retained. The authoritative
`physics.pose_input.turret_yaw` drives the existing turret/gun meshes and cockpit
HUD arrow. Drawing never rotates the game state. The single-player owner holds
the source player-slot cooldown/latch; no multi-player manager was added.

The C turret oracle covers **150 branch cases + 48 sequential updates / 1,584
fields**, including arithmetic limits. The clock/Player/prefix/physics oracle
now includes actual turret poses: **15 sequences / 180 updates / 1,176,942 fields**.
Native/Wasm agree on 24 updates, and the browser checks actual Z keydown/release.
Existing 2,880 physical updates / 18,041,268 fields and four native checkpoints
remain unchanged without turret actions. Workspace **115 passed / 70 external
ignored**, lints, 191 GPU cases and production pass. Wasm **837,234 B** without
test probes. Logs: `build/rust/tests/m5-2-turret-*`; select `reference turret
race-clock vehicle-runtime` and use `drive ... --scenery --chase --turret`.

Next integrate real inventory/fire/projectile/pickup ownership; M5.2 as a whole
remains open.

The actor ownership prerequisite is now complete: constructor-produced state
is moved once into `Scene.components.actors`, alongside its actual colliders and
memberships. `scenery::Loaded` retains decoded resources and construction evidence,
not live actor state. Background linkage still runs at its original position,
before publishing the store and returning startup. No actor tick/no-op, RNG draw,
copy of live state, or extra query cache was added. The complete construction
oracle still passes 25 starts / 4,743 constructions / 111,065 fields. Workspace
115 passed / 70 external ignored, native/all-feature Wasm lint and production
compilation pass; Wasm is 837,641 B. Browser behavior was qualified at the preceding
turret checkpoint; this ownership-only step used focused constructor/compile
checks. Evidence: `m5-2-owners-*`. Actual pickup updates/collection remain next.

### M5.2c Powerup movement module

`powerup` implements the actual inactive/proximity, falling, landing, terrain
conformance, parachute descent, blinking and settled spin phases. The class
dispatch updates its authoritative actor/collider/flags and publishes immediate
grid/custom-render membership at activation; auxiliary dispatch runs before the
final blink update. Removal retires the actor entry. The sorted actor store keeps
identity lookup deterministic without a per-frame scan/copy or new map cache.

The isolated original dispatch test constructs real full worlds and compares
ENGLAND, BIO and EAST: **87 short updates / 4,731 fields**, including matrices,
parachute, strict activation boundary, frame-based normalization, grid/custom
list order and RNG. This is module qualification, not full-world update parity.
The cold-world API remains read-only; unsupported actor methods still fail.
Collection/inventory/use are not implemented and the normal driving loader still
defers Powerup. Do not enable a successful no-op collection callback to show boxes.

`reference powerup`, `reference world-startup`, workspace tests (115 passed / 71
external ignored), native/Wasm lints and production compile pass. The complete
startup remains 25 selections / 4,743 constructions / 111,065 fields. Wasm is
842,491 B. This movement-only step did not change browser rendering/physical
kernels or repeat GPU testing. Logs: `build/rust/tests/m5-2-powerup-*`.

`tests/reference_world_startup.c` links unchanged native C libraries and observes
real dispatch through a linker wrapper. No constructor is stubbed or replaced.
The Rust reference covers **25 starts**: all 22 usable map/CUP selections with
vehicle 1, plus ENGLAND vehicles 5, 10 and 15. **4,743 authored constructions /
111,065 semantic fields** agree, including per-record RNG. Both unused CIRCUIT
CUP selections fail explicitly. The separate M5.1a oracle still checks eight
starts / 72 route fields.

Native/Wasm construction summaries agree for ENGLAND, SWAMP and BIO. Ordinary
integration tests check real owner/source relationships, complete record loading,
route ownership and zero updates. Existing scenery C regression covers 348
updates / 8,103,619 fields; model initialization covers 60 starts. The four saved
native driving checkpoints, browser input/inspection/camera sequences and 191
GPU cases remain passing. These are branch/startup and short-sequence checks,
not a long complete-race replay.

Workspace tests report **111 passed / 67 external references ignored** by the
ordinary entry. Selected references above ran separately. Native and both Wasm
feature lints pass; production Wasm is **818,231 B** and exports no test probes.
Original C/host/browser/assets are unchanged.

Commands:

```sh
node tools/test_rust.mjs reference world-startup race-startup scenery-runtime vehicle-model
node tools/test_rust.mjs unit
node tools/test_rust.mjs lint
node tools/test_rust.mjs web
node tools/build_rust.mjs
```

Set `CC` when GCC is not on PATH. Logs: `build/rust/tests/m5b-*`; the selective
runner records fixture/library hashes and linker flags in `references.json`.
The complete construction oracle compares semantic fields; it does not qualify
all actor data, original allocator layout, complete race globals or full-frame
image/game parity.
