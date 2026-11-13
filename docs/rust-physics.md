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

Drive checkpoint: `1416a89`.

## Exact basis math, attitude and velocity

`math::Matrix` keeps the original Q12 basis as nine typed words: rows describe
body axes in world space; local-to-world uses its transpose. Euler construction,
axis rotation, pivot rotation, row-then-column normalization and angle lookup
preserve their distinct wrapping/truncating/arithmetic-shift rules. A zero raw
angle skips rotation; a nonzero full turn still performs the original arithmetic.
The last angle-table entry intentionally aliases zero, not the ideal atan value.

The sine/cosine and atan constants are shared by all maps. They are committed
as **41,474 bytes** of signed-16 data, widened for original arithmetic; cosine
aliases the extra quarter cycle rather than assuming perfect periodic symmetry.
`node tools/export_rust_math.mjs` imports them from the qualified mathematical C
catalogue, not a PE/memory capture. Rust const-decodes them at compile time.
Production Rust builds require neither this importer nor a C compiler, and the
physics runtime computes no host sin/cos/atan. Other lookup tables are deferred
until their camera/scene consumers migrate. These are global constants, not
additional per-map acceleration.

`attitude::State::step` implements acceleration-to-rotation, initial yaw, old-basis
flip guards (including cross-axis rate use), pivoted pitch/roll and unsigned
frame%10 normalization. Its config consumes the authored suspension gains.
`vector` provides the original signed approximate normalization/cross/yaw.
`velocity` preserves both precision branches and rearweight yaw correction.
The large-velocity flag is computed once from incoming world velocity and reused
on both directions; the large branch compares **signed X and Z**, not magnitudes.

56,025 C comparisons / 456,828 output words pass: every sine/cosine word, all
atan entries and quadrants, Euler/axis/pivot operations, normalization partial
errors, 8,192 attitude short-sequence records, 8,192 precision transforms and
vector/yaw operations. Deliberate angle/normalization failures verify prefix
writes. Ordinary tests explain the nonideal lookup/whole-turn behavior, old-basis
flip guards and signed large-velocity selector. Native and Wasm target checks pass.

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_math.c build/native/libtankracer_core.a -o build/native/math_reference
```

```powershell
$env:TANKRACER_MATH_REFERENCE=(Resolve-Path build/native/math_reference.exe).Path
cargo test -p tankracer-game --test math_reference -- --ignored --nocapture
```

The pure attitude stage exposes its C-equivalent prefix on error. The future
vehicle orchestrator must also preserve the original adapter's separate commit
boundary: its copied attitude result is written to the vehicle only on success.
Next: part poses and model resources, articulation/hull and motion/collision,
then compose the complete vehicle and browser proxy. The Rust vehicle is not yet running.

Math/attitude checkpoint: `50bb0fe`.

## Part poses, articulation, lean and contact hull

`pose::State` updates body/reference, both tracks, turret, gun, under/end
attachments, bottom point, base and physical position. `pose::Config::from_tank`
normalizes authored offsets by body/turret in the original order; the bottom
offset is deliberately left unadjusted. Track length still needs model bounds.
External orientation is explicit (`None`, current body or a custom basis), so the
original alias to the newly copied body basis does not become a stale snapshot.

Track travel/angle lookup, turret recoil-mode scaling, signed-16 yaw, external
recoil transform and gun pivot rotation preserve their separate intermediate
bases. Two wobble sources add to body Y, but only the last wobble value is removed
from track Y. Base position is saved before the final body-to-physical copy; this
order persists across ticks. A second-track angle error preserves the first track
and early body updates, without committing physical position or later parts.
Pose refresh intentionally replaces the body-position temporary from physical
position; the eventual vehicle orchestrator must not merge the preceding attitude
pivot's temporary position into physical position on its own.

The runtime keeps nine basis words. Legacy three-word matrix padding copied by
the C pose routine is omitted: neither physics transforms nor the existing mesh
sinks consume it (`frame_present.c` consumes only the first nine matrix words).
The oracle compares semantic pose state, not this dead memory padding.

`body` separates articulation/lean state from mutable mesh views. Missing gun
geometry skips articulation; duplicate selected vertex IDs retain last-write
order. Recoil/pitch restoring terms, sign-dependent shifts, damping, clamps and
four vertex-Z writes match C. Lean clamps before integration and can overshoot
afterwards. The contact hull retains original [1,3,2,0] point order, saved gap,
short-coordinate narrowing and radius wrap. Its position expression cancels
current position algebraically in the 32-bit wrapping ring, eliminating repeated
work while preserving results; no cache or approximation is introduced.

26,624 C comparisons / 1,191,936 words pass: 8,192 four-tick pose records,
8,192 articulation records (including actual vertex writes), 8,192 lean records
and 2,048 hull records. 716 intentional pose errors validate partial updates.
Coverage includes timer/ambient wobble combinations, all recoil-mode branches,
external-basis aliases, signed/overflow boundaries and repeated/absent mesh cases.
Ordinary tests explain wobble accumulation, error commits, duplicate vertices,
lean overshoot and hull cancellation. Native and Wasm target checks pass.

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_pose_body.c build/native/libtankracer_core.a -o build/native/pose_body_reference
```

```powershell
$env:TANKRACER_POSE_BODY_REFERENCE=(Resolve-Path build/native/pose_body_reference.exe).Path
cargo test -p tankracer-game --test pose_body_reference -- --ignored --nocapture
```

Next: SS/SM resource decoding and model-derived contacts/bounds, mutable damage
geometry, motion/collision, then complete vehicle construction/integration and
browser proxy. Poses are verified in isolation; no full running vehicle is claimed.
These changes add per-vehicle state and mesh views, no map acceleration data.

Pose/articulation checkpoint: `249e5d2`.

## Model-derived vehicle construction

`vehicle_model::VehicleModel` derives physics metadata from actual SS/SM models
and the authored tank configuration. It preserves the constructor's offset
normalization order, model-bound inclusion, left/right track contact corners,
auxiliary contact placement, track dimensions, aerial height and expanded
collision/query bounds. `pose_config` consumes these normalized offsets and
real track length directly. No inferred box replaces the original geometry.

The assembly also identifies the four gun articulation vertices and original
Z values, body/turret damage quadrants, mutable shadow geometry and track UV
groups. Strict comparisons, first-wins ties, small fixed group capacities and
signed narrowing follow C. Controller setup captures alternate/reset values
before mode-specific or ambient changes, preserving their original order.

The world-scoped model bank shares immutable templates. Its case-insensitive
name cache intentionally preserves the first namespace binding, matching the
original cache. Player body/turret and every gun get instance-owned geometry;
tracks remain shared. Doubling an instance gun radius or modifying a player
body cannot mutate the cached template. Optional-module eviction and world
lifecycle are still later work, not implemented by this cache alone.

**60 complete native game initializations** (all fifteen tanks, ENGLAND and
CIRCUIT, CUP modes 0/1) match the Rust contacts, dimensions, offsets, bounds,
damage/articulation selections, shadow vertices/colors, controller coefficients
and track UV references. This oracle runs the existing full C host constructor;
it does not stub game logic. Modes 4/5 and ambient setup are ported but are not
claimed as covered by these sixty full-start comparisons. Ordinary tests check
real-model contact values, all fifteen assemblies, template isolation and
cross-namespace cache aliases. Native/workspace and Wasm Clippy checks pass.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_model.c build/native/libtankracer_host.a build/native/libtankracer_core.a -o build/native/vehicle_model_reference.exe
$env:TANKRACER_VEHICLE_MODEL_REFERENCE=(Resolve-Path build/native/vehicle_model_reference.exe).Path
cargo test -p tankracer-game --test vehicle_model_reference -- --ignored --nocapture
```

Next: mutable damage, motion/collision and complete per-tick vehicle orchestration,
then the browser proxy. Model construction is verified; a complete moving Rust
vehicle is still not running. No new per-map acceleration data is introduced.

Model construction checkpoint: `2099509`.

## Damage and collision response

`damage` implements one-shot body/turret quadrant dents, saturating track wear
and incident-face normal repair. World hits use the actual part pose; zero X/Z
belong to the original nonpositive quadrants. Flags commit before mesh access,
and different quadrants may select and repeatedly deform the same vertex.
Body Y remains unchanged; turret Y uses different front/rear gains. Face repair
normalizes both edges, takes their ordered cross product, then normalizes again
using the original signed integer approximation. It intentionally leaves optional
imported vertex normals, material/UV/color data and unrelated faces untouched.

`collision` ports wall reflection, default/explicit local and final gains, two-
tank impulse exchange, approach prediction and direction classification. Wall
separation adds its outward push even when not approaching. Low-strength pair
exchange transforms all three world components, including Y from tilted bases.
The strong branch retains the original unequal peer impulse, speed cap and the
repeated peer-X damping. Binary 1.7 conversion preserves x87 truncation through
exact integer arithmetic, including multiples of ten; no host floating point.

`ground_collision` performs the ordered four-contact pass: a miss reverses X/Z
at most once, boundary contacts reflect/update yaw, and the final boundary corner
selects high-speed track damage. Yaw uses the initial velocity magnitude, not a
later reflected velocity. Strict 150,000/450,000 thresholds, invalid/sentinel
corner handling and optional external-basis transforms are preserved.

Ground effects are required **synchronous game operations** through `Effects`,
not a deferred render queue. Failure stops before the current reflection and
keeps previous contact/effect writes. Particle/RNG implementations still need
their own migration: this checkpoint tests that service boundary with an ordered
stateful probe and deliberate errors, while track/body damage uses the actual
new Rust module and actual C functions. No built-in no-op service hides this gap.

Verification: **9,600 damage/ground records / 4,721,280 output words** match C
using all fifteen real vehicle models and four-tick sequences. Of these, 3,840
exercise the composed ground stage, including 261 error results and 948 impact
calls. Coverage includes signed-16 geometry extremes, degenerate faces, duplicate
damage vertices, track saturation, noncanonical flags, rotating/wrapping poses,
contact mixtures and callback-prefix writes. All mutable vertices, complete face
records, flags, velocities, yaw and effect-probe traces are compared.

An additional **20,720 collision records / 143,600 words** match C: 8,192 wall
responses, 8,192 pair exchanges, 2,063 binary-constant cases, 225 direction cases
and 2,048 approach predictions. Wall tests cover all four gain-presence pairs,
mode/player damping, separating/reflecting branches and 96 arithmetic errors;
error output remains untouched. Pair cases include gentle/strong exchange,
explicit cap fixtures, tilted bases and saved peer inputs distinct from current
peer state. Ordinary tests explain one-shot/saturation behavior, partial failures,
vertical impulses, unconditional wall push and nonideal binary constants.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_damage.c build/native/libtankracer_core.a -o build/native/damage_reference.exe
$env:TANKRACER_DAMAGE_REFERENCE=(Resolve-Path build/native/damage_reference.exe).Path
cargo test -p tankracer-game --test damage_reference -- --ignored --nocapture
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_collision.c build/native/libtankracer_core.a -o build/native/collision_reference.exe
$env:TANKRACER_COLLISION_REFERENCE=(Resolve-Path build/native/collision_reference.exe).Path
cargo test -p tankracer-game --test collision_reference -- --ignored --nocapture
```

Runtime stages allocate no memory. Damage scans the small existing face array
only on a newly damaged quadrant; no adjacency cache or per-map acceleration is
added. These checks establish module semantics, not a complete collision world or
running vehicle. Next: object/tank queries and overlap, obstacle/deformable game
dispatch, then position integration and full per-tick vehicle orchestration.

Damage/ground response checkpoint: `d6b93bd`.

## Ordered object queries

`object_query` ports segment/box intersection, the 8x8 obstacle grid, ordered
deformable candidates, strict tank contact overlap and nine-slot tank selection.
Colliders, entity IDs and candidate handles are typed; no original linked-list
addresses are used at runtime. Bounds are shared with vehicle construction.
Queries borrow the world's candidate arrays and allocate nothing. Creating and
maintaining those arrays from placed resources is still part of world startup.

The first valid candidate wins, not the nearest. Grid selection uses the start
point, wrapping arithmetic, x86-masked shifts and clamping to the nearest cell.
Broadphase retains the source's approximate metric, strict radius checks and
inside-box backtracking. Segment intersection preserves its two-unit tolerance,
axis tie order and partially written point on a later miss. Only a crossing
writes normal. A 65,536-candidate work limit remains; malformed handles are
explicit typed errors, while linked-list cycles are structurally absent.

Obstacle hits return world point/normal and require a strictly facing crossing;
deformable hits deliberately return local coordinates and accept inside starts.
Tank overlap ignores Y bounds but transforms all three contact coordinates.
The querying tank's active flag gates the search; peer active flags do not.
Peers require their ready flag and must not be collision-disabled. Tank inside
hits update metadata only, preserving caller point/normal. Tank misses clear
metadata; obstacle/deformable misses preserve the entire supplied hit.

**12,288 synthetic C comparisons / 89,088 words** pass: 8,192 segment cases
(including seven arithmetic errors), 1,024 obstacle queries, 1,024 deformable
queries, 1,024 tank queries and 1,024 overlaps. Coverage includes overflow/shift
boundaries, stationary/short/inside segments, changing candidate and slot orders,
rotated/absent external bases, self exclusion and partial output preservation.
Ordinary tests also cover the exact work limit, invalid handles, inactive peers
and the different miss contracts.

Additionally, **15,480 real-world queries across 664 colliders** match all eight
CUP maps with opponents enabled. The oracle starts the actual C game and runs
one real frame before exporting typed collider/list inputs and C query results:
peers only become query-ready after their first actual update. It does not force
flags or use a substitute collision implementation. Samples include stationary
origins, both directions along each world axis and all grid boundaries. Successful
crossings: 3,406 obstacles, 266 deformables and 38 tanks; 18 tank inside hits also
match. This qualifies querying real initialized state, not Rust world construction
or a complete frame update. C memory extraction is confined to this test oracle.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_object_query.c build/native/libtankracer_core.a -o build/native/object_query_reference.exe
$env:TANKRACER_OBJECT_QUERY_REFERENCE=(Resolve-Path build/native/object_query_reference.exe).Path
cargo test -p tankracer-game --test object_query_reference ordered_queries -- --ignored --nocapture
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_object_world.c build/native/libtankracer_host.a build/native/libtankracer_core.a -o build/native/object_world_reference.exe
$env:TANKRACER_OBJECT_WORLD_REFERENCE=(Resolve-Path build/native/object_world_reference.exe).Path
cargo test -p tankracer-game --test object_query_reference all_eight -- --ignored --nocapture
```

Next: tank overlap response and collision dispatch, obstacle/deformable game
operations, then complete motion/update orchestration and the browser proxy.
Existing map-budget measurements are unchanged: this stage adds no spatial cache
and does not yet attach these candidate arrays to the live Rust scene.
