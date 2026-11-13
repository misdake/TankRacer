# M3 physics checkpoints

## Primary terrain graph and queries

`assets::terrain` decodes the original ordered primary collision chain, 8x8
world grid, per-node 8x8 bins and byte-indexed face lists into typed arrays.
Collision-only/hidden render geometry is retained. Collision meshes are decoded
once; no guest address space, per-field byte callbacks or original executable
is present in the Rust query path. IDs are dense; source offsets remain provenance.

The existing index is retained before inventing new acceleration. The primary
checkpoint occupied 97,424–195,390 native bytes/map. With the secondary graph
below, total decoded collision data occupies 174,534–329,640 native bytes
(including headers/alignment/capacity). This is conservatively charged to the
same map budget as the renderer; later contact indexes must also share it.

`game::terrain` implements the original selection and three output contracts:

- First containing candidate in original order wins; no nearest/highest sorting.
- Signed shifts, 32-bit wrapping products/sums and truncating division are exact.
- Full query: a miss modifies only height; success retains the upper flag word
  and writes extra values only when the mesh's flag permits it.
- Height/normal-height retain their distinct `ny==0` behavior (vertex X is used).
- Division by zero/INT_MIN divided by -1 is an error, with original output timing.

The oracle runs these C functions against a fully initialized native game for
all eight real maps. Initially 166,740 points (expanded to 218,594 when fallback
mesh vertices were added) cover face interiors, vertices and ±1/±7
physics-unit quantization boundaries, root-grid boundaries and extreme integers.
All three interfaces/statuses/partial outputs match. Small generated BSM fixtures
add flat/vertical planes, division overflow, owner/extra flags; they pass through
the qualified C shape loader without a material stub returning success.

```sh
gcc -std=c11 -O2 -Isrc -Ihost tests/reference_terrain.c build/native/libtankracer_host.a build/native/libtankracer_core.a -o build/native/terrain_reference
```

```powershell
$env:TANKRACER_TERRAIN_REFERENCE=(Resolve-Path build/native/terrain_reference.exe).Path
cargo test -p tankracer-game --test terrain_reference -- --ignored --nocapture
```

Ordinary native unit tests also cover priority, miss preservation, upper flag
preservation, conditional extra outputs and vertical-plane distinctions without C.
The oracle logs callback counts/timing for diagnostics; these are not a claim
of a whole-frame speedup. No extra spatial table or result cache has been added.

Primary terrain commit: `7e4594b`.

## Auxiliary sampling and four-point suspension

The next layer is pure Rust state, separate from immutable map data:

- `sampling::Transform` converts a local contact to raw world coordinates with
  the original column-major Q12 products, sums and wrapping before shifting.
- `AuxiliaryContact::sample` classifies its gap: below -75 is penetrating,
  [-75,75) is touching, >=75 is airborne; a missing plane is kind 3. A failed
  query updates world coordinates but preserves classification/cache fields.
- `contact::State::step` applies the input force, squared-travel restoring term,
  damping, travel dead step and bounds, then output/tilt. Gains are typed
  multiplier/shift pairs rather than packed words. Units remain the game's
  fixed-point units; these names do not claim SI spring constants.
- `suspension::State::step_resolved` updates points in original order. Geometry
  is queried lazily only for an uncached side with nonpositive input. A positive
  force can therefore intentionally use the previous auxiliary classification.
  Resolver errors preserve exactly the state already committed by earlier points.
- Aggregation applies tilt to angular state and combines same-sign point loads.
  Maximum penetration gates body correction, but the **last** overflow supplies
  the lift amount. Downward velocity halves towards zero; a positive timer can
  become negative after subtracting dt. Neither behavior is "cleaned up."
- Contact layout/load and the shared primary-material comparison are explicit
  functions. Load must be sampled before suspension changes travel.

The independent C oracle compares 26,153 records / 738,765 output words,
including all corner/auxiliary gates, force signs, travel bounds, shift masking,
overflow, 625 contact-kind combinations, lazy resolver errors/partial writes and
four-tick sequences. Auxiliary sampling adds 11,172 exact C comparisons across
all eight maps and a vertical-plane error fixture: ±75 boundaries, misses,
rotations (including wrapping products), forced material and preserved outputs.
Small ordinary Rust tests document damping, tilt, lift, timer and lazy-query rules.

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_suspension.c src/physics_contact.c src/physics_suspension.c -o build/native/suspension_reference
```

```powershell
$env:TANKRACER_SUSPENSION_REFERENCE=(Resolve-Path build/native/suspension_reference.exe).Path
cargo test -p tankracer-game --test suspension_reference -- --ignored --nocapture
# Rebuild terrain_reference using the command above before the auxiliary test.
cargo test -p tankracer-game --test terrain_reference auxiliary -- --ignored --nocapture
```

These modules add fixed per-vehicle state, no map cache/index. This is **not a
working vehicle** yet: deformation/contact integration, configuration, drive/pose
and collision remain, then the browser proxy. Gameplay
still uses the existing C reference; no whole-frame performance claim is made.

Contact/suspension checkpoint: `9f638a2`.

## Secondary grid and fallback normals

`TerrainData::fallback` owns the second ordered 8x8 root grid and sector nodes;
collision meshes and face-index storage share the primary decoder's arrays.
The two root grids use the same validated parser. Cyclic chains, invalid grid
offsets, missing face centers and invalid face indices are errors at load time.

`geometry::fallback` follows the original sector rectangle/radius tests and flag-4
mode filter. It compares stored triangle **bounding-box centers**, not centroids,
surface intersections or Euclidean distances. The metric is largest unsigned
magnitude plus one quarter of each other magnitude, then interpreted as signed
32 bits. Only a strictly smaller result than the current best (initially 300000)
replaces the candidate. A miss preserves the output normal. No allocation,
byte-address callback, new spatial index or cache is used during queries.

The C comparison covers 535,896 real-map queries around all secondary mesh
centers, quantization boundaries, radius bounds, far heights and integer extremes.
2,800 fixture queries additionally prove equal-distance source ordering, a
successful mode-2/flag-4 selection (not present in the sampled shipped geometry),
the strict cutoff and unsigned/wrapping magnitude behavior. All outputs match.
The expanded primary/auxiliary oracles also pass after the decoder change.

```powershell
# Rebuild terrain_reference first; all C production sources remain unchanged.
cargo test -p tankracer-game --test geometry_reference -- --ignored --nocapture
```

Secondary-grid checkpoint: `e8e5e71`.

## Deformation and composed contact pipeline

`deformation` separates local geometry/bounds from the object's changing pose,
activation records and dirty state. A sample performs the original row-major
inverse transform, sign/magnitude shift and sequential X/Z clamp. It selects the
maximum positive vertex height within strict ±128 windows, activates state-0
records unconditionally or state-1 records within strict ±750 raw-coordinate
windows, writes their state/color and applies the signed edge attenuation.
The 6,144 three-sample fixture records / 270,336 output words match C exactly,
including wrapping arithmetic, reversed clamps, window boundaries, unchanged
record positions and color/state mutations. No per-query allocation is made.

`contact_pipeline` now composes primary terrain sampling, spatial noise, temporary
surface effects, deformation, materials, fallback normals, contact layout and
lazy auxiliary/suspension response. Dependencies remain explicit: immutable map
and coefficients, track poses, contact config, mutable object state and a **shared
game RNG seed**. There is no original-memory read/write interface in this runtime.
Game-owned effect-slot clearing and device feedback are explicit notifications.

Important ordering/output contracts:

- A miss retains raw height/flags/previous gap and restores the saved gap; it
  clears only that sample's material/node before possible fallback classification.
- Surface disturbance uses truncation towards zero, unlike the arithmetic-shift
  gains in suspension. Noise is a spatial lookup, not a fresh RNG draw.
- Primary samples see the effect's old positive timer. The signed-16 timer and
  shared RNG advance afterwards, so the expiration tick still disturbs height.
- Load uses the four original travel values before suspension changes them.
- Materials compare the same primary pair for both tracks. Fallback normal
  writes and stale auxiliary caches keep their original timing.
- Errors preserve earlier point updates, resets and already emitted notifications;
  they do not advance later stages. Deformation state is shared across all four
  samples and updated immediately, not collected and applied at frame end.

The composed oracle starts the full native game for each real map. It receives
explicit pre-state/config, performs its own C geometry queries and composed
update, and returns all state fields plus effect/deformation writes. It does
not supply post-state contact classifications to Rust. 6,144 records across
eight maps match 917,504 state words, covering all contact kinds 0..4, four-tick
sequences, mixed materials, cache reuse/refresh, deformation and effect expiration.
118 injected vertical-plane errors verify partial state and RNG timing too.
Ordinary unit tests also explain expiration, pre-response load and missing-resource
errors without depending on the optional C executable.

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_deformation.c build/native/libtankracer_core.a -o build/native/deformation_reference
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_pipeline.c build/native/libtankracer_host.a build/native/libtankracer_core.a -o build/native/pipeline_reference
```

```powershell
$env:TANKRACER_DEFORMATION_REFERENCE=(Resolve-Path build/native/deformation_reference.exe).Path
$env:TANKRACER_PIPELINE_REFERENCE=(Resolve-Path build/native/pipeline_reference.exe).Path
cargo test -p tankracer-game --test deformation_reference --test pipeline_reference -- --ignored --nocapture
```

These are typed physics stages, not yet a loaded/running Rust vehicle. The
resource loader still needs to populate their configs/poses/records and validate
their resource bounds (including the original 65536-record deformation limit).
Contact composition checkpoint: `53a558b`.

## Authored surface and vehicle configuration

`assets::config` loads SURFS.DAT and all 57 TANK*.DAT keys directly into named
resource types: ten part nodes/offsets, drive parameters, suspension parameters
and attachment offsets. The runtime has no object-offset field map. Gain WORDs
and damping WORD/BYTE/BYTE descriptors are distinct; numeric stores preserve
their original wrapping widths. Duplicate keys win last, names are ASCII casefolded,
comments/quoted tokens are supported. Malformed/missing values are rejected at
resource load; this is not an attempt to preserve unsafe malformed-file parsing.

`Tank::resolve` applies explicit player/special, battle/ambient, mode-5 backup
and CIRCUIT overrides in their original order. Derived suspension travel extents
retain 32-bit multiplication/shift and validate the contact module's bound order.
`contact::Config` and `contact_pipeline::LoadConfig` now consume these resources;
half the composed oracle corpus uses the authored suspension and surface data.
The spatial-noise initializer consumes sixteen shared RNG values as in C.

The C oracle compares all 100 surface factors and 15 authored tanks plus a
width/casefold/quoted-name fixture in 32 override contexts (512 tank cases).
This includes node names/vectors, every stored coefficient, derived extents,
sidegrip reset capture and mode-5 backups. All fields match. The 6,144 composed
contact records still match after switching half to the real resources.

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_config.c build/native/libtankracer_core.a -o build/native/config_reference
```

```powershell
$env:TANKRACER_CONFIG_REFERENCE=(Resolve-Path build/native/config_reference.exe).Path
cargo test -p tankracer-assets --test config_reference -- --ignored --nocapture
```

Typed-config checkpoint: `342460a`.

## Body damping, engine and traction

`drive::State` owns body-space velocity, angular rates, track/engine speed, boost
target, slip, gear, acceleration and current load. `Input` and `Environment`
explicitly supply controls and sampled contacts. `Tuning::default` names the
original gear/damping constants; the oracle independently checks all eighteen
words against the immutable C catalogue before comparing steps.

`State::step` ports the full drive stage through steering/understeer. It allocates
no memory and makes no address callbacks. Exact details include signed WORD
forces, 32-bit products, masked shifts, per-surface truncation before arithmetic
averaging, weather selector priority, and alternate yaw's reuse of the forward
damping descriptor's high bytes. Track decay, body damping and target approach
have distinct formulas and stay distinct. The strict gear thresholds and bounded
top-gear loop retain the C error/partial-output contract. Launch, boost, drag,
braking, special double-traction and fallback-contact branches are all present.

The drive oracle loads each authored tank and SURFS.DAT through C, applies the
same explicit overrides, then independently updates the requested pre-state.
9,600 records across all fifteen tanks and four-tick sequences match all sixteen
state fields/statuses exactly. Coverage includes all five gears, all seven dry/
rain/snow slide selectors, hill roll, ordinary/alternate controls, launch/boost,
airborne/fallback contacts, integer boundaries and x86 shift masking. Deliberate
invalid weather and gear-limit fixtures prove error timing too. Ordinary tests
explain threshold strictness, signed averaging and alternate descriptor reuse.

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_drive.c build/native/libtankracer_core.a -o build/native/drive_reference
```

```powershell
$env:TANKRACER_DRIVE_REFERENCE=(Resolve-Path build/native/drive_reference.exe).Path
cargo test -p tankracer-game --test drive_reference -- --ignored --nocapture
```

Next: model/pose resources, exact attitude/yaw/world-velocity/motion and collision,
then vehicle construction and browser proxy. Contact and drive are verified stages,
not yet a complete moving Rust vehicle. No new per-map acceleration or whole-frame
performance claim; top-gear work can be profiled once the complete frame runs.
