# M5 single-player race

Updated 2026-11-17. M5.1a route binding and M5.1b ordered construction are
qualified within the boundaries below. The browser still runs the accepted
driving scene; the complete race clock and full-game frame parity remain open.

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

Next, M5.1c must introduce the actual race/session owner and countdown gates.
Audit `frame_game_tick_menu_limit`: race timing precedes entity ticks; camera
post-update dispatch follows them, then queue drain and session actions. Do not
move the accepted physics tick or use the driving subset's seed as full-startup
state. M5.1d owns progression/finish/restart/exit; M5.2/M6 must supply real runtime
owners before complete-world updates can claim parity.

## Verification

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
