# M5 single-player race

Updated 2026-11-17. This extends the accepted driving scene. Complete race
startup and full-frame original-game parity are still unqualified.

## Delivery order

| Slice | Required result |
| --- | --- |
| M5.1a route startup | Real Player owns its starting checkpoint after authored routes are linked; native/browser use shared loading; compare semantic startup fields with complete C startup. |
| M5.1b ordered startup | Reconcile the authored Player/fallback path, camera/finish-owner creation, constructor order and shared RNG. Trace omitted constructors before deciding which later-stage dependencies are needed. Do not substitute RNG skips or placeholder entities. |
| M5.1c race clock | Integrate original countdown/release/driving gates and elapsed time at their real update positions, with short C/native/Wasm boundary sequences. |
| M5.1d progression/lifecycle | Checkpoint/lap records and associated object restoration, timing, finish, restart and exit through actual owners. |
| M5.2 interactions | Pickups, weapons, damage/destruction and visible effects/HUD through the same race owner. |

Optional scenery behavior/configuration stays M6; opponents, complete CUP menus,
audio and all-map presentation stay M7. If their construction is a prerequisite
for startup parity, record that dependency explicitly. Do not silently promote
an accepted component test into complete world/race parity.

## Source order and current boundary

The readable C reference is preserved. Relevant ownership is visible in:

- `src/frame_game.c`: global initialization, resource/environment preparation,
  authored world load, route linkage and transient preparation; optional opponent
  load; fallback Player/camera/model initialization; nine racer slots; route
  initialization; finish-owner creation; session begin.
- `src/frame_player_lifecycle.c::frame_player_routes_initialize`: CUP players
  select their nearest route point using physical position. The AI-only slots
  additionally initialize progress, and non-CUP branches have separate behavior.
- `src/frame_race.c`: countdown/release gates and elapsed lap time.
- `src/frame_player_lap.c`: route crossing, lap events and object restoration.
- `src/frame_game.c::frame_game_tick_menu_limit`: race timing before entity
  ticks, camera post-update dispatch after those ticks, then queue drain and
  session actions. The accepted standalone chase-camera tick remains unchanged
  here; full race scheduler integration must audit these distinct phases.

Rust currently loads the supported scenery subset in file order and constructs
its Player afterwards, using the first authored Player transform or its existing
fallback. It does not yet reproduce all world actors, original identity order,
camera/finish-owner entities or complete shared-RNG initialization effects.

M5.1a adds only the actual Player route state (`checkpoint`, initial passed count)
and binds it through `session::loading::Prepared::finish` after Player creation.
`waypoint::View::nearest` supplies the original query and priority/boundaries.
The result lives on Player; it is not a renderer lookup. Low-level physics-only
fixtures without scenery keep an unbound route. No route advancement, countdown,
race state machine, lap event or future plugin behavior was added in this slice.

## Verification

The complete C startup oracle (`tests/reference_race_startup.c`) runs the original
native startup for all eight default CUP selections with vehicle 1 and no opponents.
It observes Player position, checkpoint presence, passed count and target
position/progress. All **8 starts / 72 semantic fields** match Rust. IDs and RNG
are emitted separately as diagnostics and are deliberately not parity claims.

A regular all-map regression checks real route ownership, unchanged startup RNG,
unchanged scenery colliders and zero world ticks. The camera-free fixture remains
unbound. Browser/native route state matches; existing short physical/input/camera
sequences, free-inspection isolation, six views and 191 GPU cases remain passing.
Workspace tests report 109 passed and 66 external-reference tests ignored by
the ordinary entry; the selected startup reference was run separately. Native
and both production/test-feature Wasm lints pass. Production Wasm is 761,096 B,
with no test-only route/probe exports. Four saved pre-structure native outputs
still match. The per-frame manual-order
oracle now includes Player route state. These are scoped checks, not a long race.

Commands:

```sh
node tools/test_rust.mjs reference race-startup
node tools/test_rust.mjs unit
node tools/test_rust.mjs lint
node tools/test_rust.mjs web
```

Set `CC` when GCC is not on PATH. Logs: `build/rust/tests/m5-route-*`;
`references.json` records the selected C fixture/library hashes.

## Startup audit still open

These are measured cumulative LCG advances from seed zero after full startup:

| Map | Complete C | Current Rust | Difference |
| --- | ---: | ---: | ---: |
| ENGLAND | 1714 | 1612 | 102 |
| MEXICO | 2044 | 2044 | 0 |
| SWAMP | 1773 | 1676 | 97 |
| EAST | 874 | 744 | 130 |
| THEME | 1738 | 1672 | 66 |
| BIO | 1204 | 1198 | 6 |
| MONACO | 1504 | 1471 | 33 |
| CIRCUIT | 1513 | 1512 | 1 |

The route binding added here consumes no RNG and leaves these pre-existing startup
differences unchanged. Source inspection identifies omitted consumption sites:
`frame_flock_construct` draws 32 values per flock, Train constructor branches draw
values for authored behavior, `frame_lap_visibility_construct` can draw for
`:lap_to_add -99`, and `frame_camera_create_chase` draws a starting-view variant.
The C/Rust test reports authored Flock counts and omitted random-lap sites.
These observations locate real missing constructor work; the final-seed delta
alone does not prove which constructor caused every difference, or equal state
when seeds happen to match. RNG-dependent branch counts can change later draws.

Next: trace constructor/identity/RNG boundaries, then integrate the required real
startup owners in order. Keep optional actor implementations under their assigned
stages unless an actual startup dependency requires an explicit scope decision.
