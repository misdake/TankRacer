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

Ordered query checkpoint: `1cec4a3`.

## Tank overlap response and collision dispatch

`tank_collision::Pair` composes strict contact overlap, per-corner impact calls,
ordered separation, track/body damage and the qualified pair impulse. Geometry
and mutable kinematics remain separate world components; damage mutates owned
meshes through a synchronous effect implementation. Original address aliases
are not carried into the runtime. Malformed/aliased tank handles are explicit
typed errors; valid world slots own unique tanks.

The four cardinal push vectors are magnitude **1024**, as verified against the
original defaults, rather than reflection's 4096 unit vectors. Other-in-self
overlap stops at its first corner; self-in-other examines all corners and emits
each eligible impact in order. Modes 4/5 suppress these impact calls. A low-speed
separation pass always runs once before checking approach or the saved initial
horizontal-speed sum plus 20,000. The work limit preserves the last completed
iteration. Track damage applies only strictly above 250,000 and below 450,000;
the high-speed branch chooses side separation versus pair impulse from both
facing codes. Each branch retains signed wrapping and the original comparisons.

`step` now connects the actual nine-slot tank query to overlap response and its
non-overlapping swept fallback. It computes the original segment from current
base position and previous physical position, captures a peer's pre-response X/Z
velocity for modes 4/5, then applies responses and finally increments the owning
tank's collision timer. Failed responses retain the captured peer and earlier
writes but skip the timer. Mode 4/5's later velocity blending belongs to the
outer vehicle update and is not silently folded into collision dispatch.

**8,192 four-tick C comparisons / 589,824 output words** match: 4,096 overlap
records and 4,096 complete query/response dispatch records. They include 765
separation-limit results, 1,156 effect-error results, 2,097 successful dispatch
hits and 6,960 impact calls. All current velocities, timers, peer snapshots,
damage flags, body vertices/face normals and ordered effect-probe writes match.
Coverage includes strict speed thresholds, zero/large time deltas, zero/rotated
bases, both owner orders, overlapping/inside/swept misses, mode-specific peer
state, saturating wear and repeated damage vertices. C's actual damage routines
are used; impact particles are still a deterministic service-order/error probe,
not claimed as migrated. Ordinary tests explain cardinal scale, the mandatory
first separation and effect failure before velocity/timer commits.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_tank_collision.c build/native/libtankracer_core.a -o build/native/tank_collision_reference.exe
$env:TANKRACER_TANK_COLLISION_REFERENCE=(Resolve-Path build/native/tank_collision_reference.exe).Path
cargo test -p tankracer-game --test tank_collision_reference -- --ignored --nocapture
```

Workspace native tests and host/Wasm Clippy pass; the qualified C sources remain
unchanged. No per-call allocation or per-map cache is added. Next:
obstacle/deformable response and game dispatch, real impact particles/shared RNG,
then motion and full vehicle orchestration. A complete moving Rust vehicle is
still not running.

Tank dispatch checkpoint: `bcdc436`.

## Obstacle/deformable response and real mesh compression

`obstacle_collision` connects the qualified queries to reflection, weighted
velocity blending, class-specific score/size updates, feedback and synchronous
method calls. The obstacle's speed threshold is checked **after** reflection:
below-threshold hits keep the reflected velocity but skip blending and dispatch.
Blending wraps each product and truncates the final signed sum by seven, across
all three components; an apparently identity weight must not erase that arithmetic.
Score gates compare exact class IDs, not inheritance or the player boolean.

Deformable handling clears the material tag before querying, commits the contacted
entity only after successful query completion, invokes feedback before reflection,
then blends and dispatches. It consumes RNG only for eligible strict-bound contact
points, and the first draw precedes the impact-pair test. Both impact calls and
their shared RNG updates finish before considering the next contact. The pair's
first index is saved before either callback, even if a callback changes forward
velocity. Post-method contact checks observe current collider and vehicle data.
Local query normals are deliberately passed through as in the source.

The mutable world owns stable candidate handles and shared class tables; a method
may change collider state or list membership synchronously. `dispatch` uses logical
Rust method IDs rather than executable addresses. It preserves the original slot
lifetime: normal/null-method success restores the saved slot, but an out-of-range
interface or failed method keeps its changed slot. Result/scene writes survive
failure. Missing classes/handles are explicit errors; no silent method stub exists.

`deformable_hit` implements the actual 408170 method: strict +/-256 raw-vertex
windows, signed-16 height narrowing and floor clamp, per-vertex lateral drift,
material overwrite, hit owner/dirty flags and the sound-trigger timer. Both sound
random draws occur even without device playback. This is separate from the earlier
surface sampling stage's +/-128 window. It does not invent normal/radius updates.

`scene_collision::Deformable` binds that real method into the collision invocation
and owns one mutable mesh for future rendering and surface-query consumers. The
method consumes **base position**, whereas the preceding query uses physical
position. A composed native Rust test makes those positions different and checks
the full query -> dispatch -> compression -> material/RNG path and slot restoration.
This binding is not yet a complete runtime method registry or world loader.

**6,144 C flow records / 141,312 words** pass (2,048 each for obstacle response,
deformable response and standalone dispatch). These include 2,653 successful
query hits, 2,674 service calls and 277 error results. The stateful service probe
changes velocities, collider positions, lists, RNG and method slots to verify
same-frame ordering, prefix commits, positive-error normalization and early gates.
Those probe methods/impacts are contract tests, not claimed gameplay implementations.

Separately, **4,096 actual C deformable-method records / 225,280 words** pass,
including 183 injected flag-read failures and 261 sound triggers. Four-tick
sequences cover all drift flag combinations, shift masking, zero/negative/large
time deltas, geometry/height boundaries, absent/rotated bases and timer expiration.
All modified vertices, material, timers, owner/dirty fields, output result and RNG
match. Missing flag data stops after the current height write, preserving the
source prefix. Ordinary tests explain strict-window and base-position behavior.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_obstacle.c build/native/libtankracer_core.a -o build/native/obstacle_reference.exe
$env:TANKRACER_OBSTACLE_REFERENCE=(Resolve-Path build/native/obstacle_reference.exe).Path
cargo test -p tankracer-game --test obstacle_reference -- --ignored --nocapture
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_deformable_hit.c build/native/libtankracer_core.a -o build/native/deformable_hit_reference.exe
$env:TANKRACER_DEFORMABLE_HIT_REFERENCE=(Resolve-Path build/native/deformable_hit_reference.exe).Path
cargo test -p tankracer-game --test deformable_hit_reference -- --ignored --nocapture
```

Shared RNG arithmetic is centralized in `random::next`; it still borrows the one
game-owned seed. Existing all-map contact/pipeline C regression (6,144 records)
also passes after this mechanical consolidation. Workspace tests and native/Wasm
Clippy pass; all qualified C production sources are unchanged.

The new stages add no spatial acceleration cache or per-call heap allocation.
Owned world/class/candidate storage will need live aggregate accounting when
connected to Wasm; it is not included in existing scene-budget measurements yet.
Next: the real breakable-obstacle method/lifecycle and impact particles, then full
motion/update orchestration. A complete moving Rust vehicle remains unfinished.

Obstacle/deformable checkpoint: `d836ea7`.

## Generic breakable hit and ordered scene membership

`breakable` now implements the actual 40A3E0 body: activity/already-hit guards,
signed approximate-speed angular impulse, reverse-direction flag, sound and
activation, saved vehicle, ordered turn-rate limits, auxiliary/effect dispatch,
spatial removal, attachment propagation and final feedback. It rereads mutable
state at the original callback boundaries. Activity can be zero while the entity
remains scheduled; these are deliberately separate concepts. Its output result
is zero even when an earlier operation fails.

The method uses the qualified class slot table and real `lifecycle::Ticks` for
the standard enable-tick binding. Attachment bindings are immutable level data
in source order, while target liveness is checked at each call. Self/cyclic
chains terminate through the original hit flag. The original 65,536 attachment
limit and state prefixes remain. Source conditional turn limits are preserved
even when minimum exceeds maximum; this is not replaced by a generic clamp.

`spatial` ports grid insertion and removal. Insertion uses the authored radius
padding, clamped center, wrapping cell bounds, strict overlap and prepend order.
It intentionally permits duplicate entries. Removal visits the stored cell's
3x3 neighborhood in row/column order and removes **one** matching entry per cell.
The per-list traversal limit includes the sentinel test; failure retains already
completed cells. `Cell` exposes signed X/unsigned Z and an optional absent value,
instead of making gameplay manipulate packed coordinate bits.

`lifecycle::Ticks` appends newly enabled entities once according to bit 16,
preserves other enable-time flags, and removes the first entry on disable.
Disabling advances a matching next-entity cursor before list removal, so even a
later traversal failure preserves that cursor write. Successful disable clears
bits 16/32. This is membership support, not yet the full entity-update/notification
scheduler or the falling/spinning/bouncing object ticks.

`dispatch::call_entity` distinguishes ordinary lifecycle dispatch from physics
dispatch: entity output commits only on success, and positive errors remain for
the enclosing scene boundary to normalize. Physics method errors still normalize
to -6; activation/sound/feedback scene errors normalize to -8. The changed slot
survives the original exceptional exits. Native logical bindings are re-exported
from `scene_collision`; no function address is executed or retained as a callable.

**8,192 C membership records / 1,923,606 words** pass: 2,048 four-tick records
each for insert, remove, enable and disable. All 64 cell lists, tick order, flags,
cursor, result and packed-coordinate conversions match. Cases cover signed/shift
boundaries, missing shape radius fallback, duplicates, strict cell edges and
1,156 remove / 556 disable error prefixes.

**4,096 C breakable/collision records / 881,784 words** pass: direct real hits
and full query -> response -> real-hit chains, including 129 errors, circular
attachment graphs and an explicit oversized attachment list. Activation uses
the actual C entity/list routines and removal uses the actual C spatial/list
routines; their allocation service is only a bounded test heap. Velocities,
flags, angular/turn values, owners, active order, all grid lists, method slot,
output result and shared RNG/probe writes match. Auxiliary/effect methods and
audio/feedback remain ordered stateful probes, **not** claimed migrated bodies.
Native unit tests explain first-match/prefix behavior and dispatch output timing.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_membership.c build/native/libtankracer_core.a -o build/native/membership_reference.exe
$env:TANKRACER_MEMBERSHIP_REFERENCE=(Resolve-Path build/native/membership_reference.exe).Path
cargo test -p tankracer-game --test membership_reference -- --ignored --nocapture
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_breakable.c build/native/libtankracer_core.a -o build/native/breakable_reference.exe
$env:TANKRACER_BREAKABLE_REFERENCE=(Resolve-Path build/native/breakable_reference.exe).Path
cargo test -p tankracer-game --test breakable_reference -- --ignored --nocapture
```

Workspace tests, native/Wasm Clippy and the existing 6,144-record obstacle/dispatch
C regression pass. All qualified C production sources remain unchanged.
No new spatial cache is introduced. Membership insertion/activation may grow
owned vectors; zero-allocation complete updates and live aggregate map accounting
are not claimed. Rare attachment lookup uses the existing collider array rather
than adding an unmeasured index.

Next: real impact particles/shared line resources, then full vehicle motion/update
composition. Falling/spinning/bouncing ticks, auxiliary/destruction effects, world
construction and the full entity scheduler remain later scene/game integration.
The complete moving Rust vehicle is still not running.

## Real impact particles and synchronous ground effects

`impact_particles` ports 42A5C0 emission and 42AAC0 animation: one game-owned
four-batch ring, ten particles per batch, shared RNG and per-vehicle cooldown.
The renderer receives retained geometry; emitting/updating/freeing that geometry
is synchronous simulation work. A full line pool still consumes all 20 RNG
draws, commits the batch and returns exhausted handles. Recycling an active
batch frees its old lines before validating the new angle; failure retains the
old batch metadata. Kinds 3/4 reset cooldown only after angle success.

Tick adds cooldown first, then processes matching batches in ring order. A batch
created in the current frame returns from the **whole function**, retaining any
earlier batch updates. Distances wrap through signed 16-bit storage. A missing
required basis fails after the first particle's distance writes. Retirement is
strictly `age > 60`, and releases/marks handles in order. Unit tests explain
these boundaries instead of merely checking successful pictures.

Forward/reverse offsets are verified original PE `.data` inputs at 531868 and
531878: `(0,0,2000)` / `(0,0,-2000)`. Read-only source SHA256 is
`e8c4f12dd3caac55c4b1b29536475b58222dc44de82e008cae0e4a9c7fd9b9bb`.
`Tuning::default` owns those six integers; builds need neither that PE nor a dump.
The C oracle receives explicit inputs, covering both the original values and
synthetic alternatives; this does not qualify the old C bootstrap's initialization
of those addresses.

`effect_pool` owns the 100 line records and shared 160 quad records. Reset changes
free markers/counts while retaining other fields. It deliberately preserves the
source's bounded exhaustion quirks: updating handle MAX aliases the last quad's
tail and eight following bytes, while releasing it can desynchronize line count
and markers. A subsequent allocation can return handle 100, whose extra record
is retained outside the renderer's scan. This is a typed, bounded representation,
not an original-memory arena. Arbitrary invalid handles/corners are rejected;
general invalid-pointer memory behavior is not emulated. Quad allocation and
other quad-producing effects are still pending.

`ground_collision::GameEffects` binds the actual particle operation and
`damage::Target` to ground response. It maps contact-record index to the record's
stored corner, which can differ. Wear, one-shot body flags, vertex dents, face
normals, RNG and particle ownership commit before the next contact executes.
Non-player wear needs no mutable body copy. No notification queue stands in for
these stateful game operations. Tank/obstacle consumers still need equivalent
bindings during complete vehicle orchestration.

**4,480 C reference records / 6,850,304 words match**: 1,024 direct pool operations,
768 emissions, 1,728 particle ticks and 960 composed ground responses. The latter
form four-ground/four-tick sequences across all 15 real vehicle bodies, including
permuted corner mappings, player/mode/speed gates and saturated wear. There are
75 dented-body cases, 90 cases retaining real particle batches and 60 composed
failure prefixes. Direct cases cover full/near-full pools, wraparound, multiple
owners, same-frame exits, null basis, partial failures and retirement. The oracle
calls the actual C impact callback, ground response, track/body damage and line
pool functions; these particular operations are no longer probe callbacks.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_impact.c build/native/libtankracer_core.a -o build/native/impact_reference.exe
$env:TANKRACER_IMPACT_REFERENCE=(Resolve-Path build/native/impact_reference.exe).Path
cargo test -p tankracer-game --test impact_reference -- --ignored --nocapture
```

Workspace tests and native/Wasm Clippy pass, as does the existing 9,600-record
damage/ground regression. Source verification confirms all 244 qualified C files
unchanged. Renderer/browser code is unchanged; prior GPU checks were not rerun.
Pool and impact ring occupy 13,488 + 1,716 bytes on the native target, shared per
game rather than per map. These methods allocate no heap memory per call and add
no per-map acceleration data. Tick reuses equal endpoint trigonometry and lazily
transforms the offset once per batch, preserving the missing-basis write prefix;
no whole-frame performance gain is claimed before integration/measurement.

Next: motion integration and complete per-tick vehicle orchestration, then the
browser proxy. Auxiliary/destruction effects, moving scene objects, world
construction, the scheduler and full gameplay remain incomplete. M3 stays open.

## Post-collision motion composition

`motion::State::finish` connects the second half of 430A30 in source order:
save previous physical position -> wrapping Q14 integration -> attitude -> gun
articulation -> gated player lean -> all part poses -> required scene callback
-> contact hull -> ready flag -> four wrapping `40/dt` rescalings.
Recoil and pitch come from the updated articulation state, not stale frame inputs.

Commit boundaries matter: attitude runs on a local value and publishes matrix,
body position and angular rates only on success. Failed angle/normalization
still leaves position integration committed, but cannot leak the local attitude
prefix. Pose errors retain their existing partial writes. A failed scene callback
retains poses, skips hull/readiness/rescaling and normalizes positive status to
-8. A successful callback may modify contacts/physical position/rates; the hull
and rescaling read those modifications synchronously. An executable unit case
demonstrates failed normalization after a partial local matrix update.

**1,078 real C update-boundary records / 215,644 semantic output words match**,
including gun vertices and callback count. The oracle starts all 15 cars on
ENGLAND and car 1 on the other seven maps (22 combinations), calls the actual
complete `physics_vehicle_step`, and captures its state immediately after the
last ground-collision delta write. It compares the actual subsequent C writes
with Rust motion composition. It does not copy the expected integration/tail
formula into a second test implementation.

Sixteen branches use up to four updates each: position/velocity wrapping,
angle failure, second-track pose failure, absent gun, player/mode lean gates,
both wobbles, null/custom external basis, frame wrap, rate overflow, partial
normalization failure and callback mutation/failure. Five failing branches
produce 110 error prefixes. Test-only injections occur before any integration
read, avoiding a stale pre-evaluated write argument at the capture boundary.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_motion.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/motion_reference.exe
$env:TANKRACER_MOTION_REFERENCE=(Resolve-Path build/native/motion_reference.exe).Path
cargo test -p tankracer-game --test motion_reference -- --ignored --nocapture
```

This qualifies the post-collision composition, **not the complete Rust vehicle
update**. Earlier contact/drive/collision stages still run in C to supply this
oracle's boundary inputs. Scene callbacks are explicit ordering/error probes;
the real active after-pose effects are not yet migrated. C collision methods and
impact generation used before capture are real C bodies, not Rust qualification
of the missing world orchestration. Test RAM remains inside the C oracle only.

Workspace tests and native/Wasm Clippy pass; the qualified C production sources
remain unchanged. Motion has no per-call heap allocation or new per-map cache.
Rendering is unchanged. Next: connect pre-contact timers/abilities, contact,
velocity/drive/yaw and ordered collision stages to this finalizer, then construct
the Rust world and run the browser proxy. M3 remains open.

## Contact and powertrain vehicle bindings

`powertrain` connects incoming world velocity, the qualified drive kernel,
rearweight yaw correction, return world velocity and the suspension's vertical
delta. Its persistent state contains only engine/local-velocity/load fields;
angular rates, drive deltas, slip and lateral speed belong to `motion::State`.
A transient drive view is published even on failure, preserving direct C drive
writes. A failure skips yaw correction, the return transform and vertical delta.
The large-velocity precision choice is taken once from incoming world velocity,
then reused even if drive crosses that threshold. Alternate steering and a
positive yaw-lock timer independently gate yaw correction.

**4,800 additional C powertrain records / 96,000 output words match** across all
15 tanks, weather selections, alternate controls, launch/boost/gears and four-tick
sequences. They include 457 changed precision classifications, 480 yaw-corrected
and 2,100 yaw-blocked successful records, 360 weather and 60 gear-limit errors,
plus invalid timestep exits. The oracle composes the actual C velocity, drive
and yaw functions. The existing 9,600 drive records also pass. Both tests use
separate fixture directories so parallel execution cannot overwrite input files.

`Pipeline::step_vehicle` binds contact sampling/suspension to the same motion,
powertrain and instance pose configuration. It derives current track transforms,
base position and shared angular/vertical/contact fields from the vehicle.
The surface disturbance's +30A alias is explicitly the **turret's local Z offset**;
its impulse fields are the same recoil/pitch spring rates used by articulation.
The player-only forced-surface gate and suspension timestep/player flag follow
the original adapter rather than accepting inconsistent duplicated inputs.

The C vehicle-contact adapter computes local values and commits them only on
success. Rust mirrors that boundary with a fixed-size pending value and local
RNG, publishing successful contact data, load, pivot, lift and impulses together.
Deformable activation remains a direct world write. Effect-slot clearing now
runs synchronously at the original sampling point, before auxiliary queries;
its failure stops the stage, leaving the local vehicle/RNG unpublished. A later
query failure retains an already successful world clear. The source's write
failure is represented by `Error::EffectSlotWrite(slot)` (reference status -1).

**6,144 additional real C vehicle-contact records / 727,040 output words match**,
using all eight actual terrain worlds and authored/synthetic suspension settings.
They include 118 terrain arithmetic failures and 200 injected world-slot write
failures, cache reuse/refresh, deformation, material/fallback branches and effect
expiration. This oracle actually calls `physics_vehicle_contact`, packing input
fields and reading persistent output fields; it does not substitute a second
copy of the wrapper formula. The original 6,144 direct sampling/pipeline records
also pass. Unit cases explain both contact rollback and drive partial commits.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_drive.c build/native/libtankracer_core.a -o build/native/drive_reference.exe
$env:TANKRACER_DRIVE_REFERENCE=(Resolve-Path build/native/drive_reference.exe).Path
cargo test -p tankracer-game --test drive_reference -- --ignored --nocapture
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_pipeline.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/pipeline_reference.exe
$env:TANKRACER_PIPELINE_REFERENCE=(Resolve-Path build/native/pipeline_reference.exe).Path
cargo test -p tankracer-game --test pipeline_reference -- --ignored --nocapture
```

Workspace tests and native/Wasm Clippy pass; 244 qualified C production files
remain unchanged. The new operations allocate no per-call heap memory and add
no per-map precomputed data. Contact uses a fixed-size pending stage copy;
whole-frame cost will be measured after the complete update runs. Browser and
renderer code are unchanged. Pre-contact timers/abilities, actual collision-effect
bindings, after-pose effects, the unified update caller and Rust world construction
remain necessary before the moving Rust vehicle can be claimed complete.

## Connected active physics phase

`vehicle::step_physics` now runs the actual contact -> powertrain -> obstacle ->
tank -> deformable -> ground -> motion chain. It owns the source's impulse
measurements, pair/ground deltas, mode 4/5 pair blending and failure boundaries.
This is the active physics **phase**, beginning after the timer/ability prefix;
it is not yet the complete game update or an independent world constructor.

Vehicle dynamics are canonical. Tank queries use refreshed resource/pose views;
pair response uses a fixed nine-entry stack array and publishes both actors'
velocity/timer writes even when the stage fails. A subsequent actor update reads
the already changed canonical velocity. Disabled tank collision still writes
the original zero delta; mode-specific peer snapshots retain their source timing.
Obstacle/deformable views likewise publish partial method/state writes before
propagating an error. Ground normal/yaw writes commit before its error returns.

`collision_effects::Bank` supplies one real particle and track/body-damage
implementation for tank, object and ground interfaces, sharing cooldowns, RNG,
ring and line pool. Source contact-record corners remain distinct from indices.
After pose, body-relative effect bindings are refreshed to the **new** reference
matrix before any animation runs. Class methods receive the same effect bank,
so auxiliary/destruction code can use these actual resources rather than a
private pool. Required scene services have no default successful substitute.
The after-pose view includes current surfaces, powertrain, motion and a writable
impact accumulator for the still-pending debris/smoke/light implementations.

**1,254 connected C records / 8,537,175 semantic output words match** across all
15 cars on ENGLAND plus car 1 on seven other maps (22 combinations). Eight
branches run up to four two-actor update pairs, alternating a player and another
active tank. There are 603 observations of writes to the other actor's velocity
and 236 observations of track wear. The comparisons include all eight initialized tank
states, body vertices/face data, gun vertices, particle ownership/cooldowns,
shared RNG/slot, peer snapshot, line records and exhaustion aliases. Cases cover
collision disable, modes 0–5, near/full pools and an after-pose failure.

The oracle calls the actual complete C `physics_vehicle_step`, capturing its
input at the first contact read after its prefix. It then compares the resulting
C persistent fields with Rust's connected phase; no copied phase formula creates
expected results. Before each compared update, inputs come from the C state:
this is boundary/short-sequence parity, not yet a Rust-only full race replay.

Scope is deliberate: test obstacle/deformable lists are empty. Object miss
stages run, but their hit/class-method integration is not qualified by this test.
Existing hit-module evidence remains separate. Contact feedback is an explicit
probe; after-pose invokes the real impact tick and an error branch, not the
complete original scene effect chain. Original impact-particle offsets are explicit
verified fixture inputs, not a claim about the old C bootstrap's initialization.
Test DATA/address handling is confined to C/test serializers; Rust production
logic contains no guest-memory arena or C runtime dependency. Common motion
serialization is shared with the earlier boundary test, which still passes all
1,078 cases after extraction.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/vehicle_reference.exe
$env:TANKRACER_VEHICLE_REFERENCE=(Resolve-Path build/native/vehicle_reference.exe).Path
cargo test -p tankracer-game --test vehicle_reference -- --ignored --nocapture
```

Workspace tests and native/Wasm Clippy pass. Qualified C production files remain
unchanged. The phase and its temporary tank views add no per-call heap allocation
or per-map precomputed cache; scene methods can still grow their owned state.
Renderer/browser behavior is unchanged and no new whole-frame performance claim
is made. Next: real scene hit bindings, pre-contact timers/abilities, complete
after-pose effects and authored world startup. In that integration, deformable
sampling and rendering must borrow the same current mesh, with contact-only
borrows ending before collision methods mutate it; retaining a copied sampling
mesh or extending that borrow across the whole phase would be incorrect. The
independent moving Rust game and M3 remain unfinished.


## Live deformable mesh in the connected vehicle phase

`scene_collision::Surfaces` owns each deformable mesh once, alongside hit state,
vertex flags and sampling records. Contact now obtains a scoped view of the
current vertex slice; that borrow ends before the collision method mutates the
mesh. No frame copy or original-memory arena is needed. Sampling derives its
pose from the collider main matrix (4073D0); hits independently retain the
optional external-basis rule (408170). Mesh dirty (+CA) and sampling-record dirty
(+CC) remain separate authoritative fields.

A same-instance test samples height 200, invokes the real hit through the class
method table, then samples height 193 from the identical vertex allocation.
It also checks record activation, separate dirty flags, method slot restoration,
material, sound event, shared RNG and missing-entity rejection.

The connected C oracle now uses VC02. One branch retains a real loaded
map deformable, relocates it under the player and executes the actual known
C method, including compression, flags, sound timer and RNG. The following
contact phase samples its modified vertices. Snapshot coverage includes the
current mesh/face bytes, collider, hit/sampling configurations and states and
record activation. Serialization now correctly treats vehicle +D8C as an entity
ID; it was previously mistaken for an object pointer, hidden by empty lists.
Unused breakable fields are not projected over deformable timer storage.

**1,254 records / 8,716,157 semantic words match**, across the same 22 map/car
combinations (all 15 cars in ENGLAND, car 1 in the other seven maps). Coverage
includes **37 real surface hits, 37 changed meshes, 33 later surface sampling
borrows**, 603 peer-velocity writes and 236 track-wear observations. Other
branches retain their collision modes, pool pressure and after-pose error case.
As before, every boundary takes its input from C: this proves connected short
sequence semantics, not a complete autonomous Rust race. Regular obstacle hits,
full world construction, timer/ability prefix and complete after-pose scene
operations remain pending.

Checks: workspace tests, native/all-target Clippy and Wasm game Clippy pass;
6,144 deformation cases / 270,336 words and both 6,144-case contact suites pass
again after the ownership change. `tools/verify_sources.py` confirms all 244
production C files unchanged (97 modules, two audio packs). Renderer code was
not changed; no new GPU or performance qualification is claimed. The scoped
view/store lookup adds no per-frame vertex allocation or precomputed map cache.
The native C reproduction command in the previous section still applies.


## Authored Crush construction and stable scene geometry

`scene_startup::Crush::build` now constructs actual class components from a
`level::Object`, archived shape bundles, the decoded terrain graph and the shared
RNG. It creates the collider/bounds/weight, lap visibility, surface material/sort
alias, sampling bounds, compression/floor/edge limits, per-vertex drift flags,
police-glow records (local and world positions, colors, state and timer), ground
height and siren envelope timer. General Obstacle type/shatter extensions are
explicitly rejected if introduced on Crush; they are not successful no-ops.

`scene_meshes::Meshes` owns current geometry behind stable handles. Source-name
bindings share geometry; `:multi_crush` makes an independent copy of the current
source. This distinction is gameplay-visible: two unqualified copies would lose
shared deformation. Visibility hints share/copy with their meshes too. Immutable
`Model` provenance remains available for materials/normals/bounds. Contact, hit
methods and future rendering all access the scene store, without RefCell or
per-frame vertex copies. The previous contact-scoped lifetime contract remains.
The same-instance sample/hit test and a new shared/private allocation test pass.

`Crush::install` moves authoritative components into the existing collision
world and class table, preserving construction order. Active objects enter the
ordered deformable list; lap-delayed ones do not. None enters the ordinary
obstacle grid. Render/scheduler metadata survives in `PlacedCrush`; complete
lap events, enter/leave/destroy/tick scheduling and rendering are still pending.
Installation rejects inconsistent stores or a missing real hit binding.

**154 actual C class constructions / 28 glow records / 10 shared-mesh pairs
match across 22 nonempty map/CUP configurations.** The two CIRCUIT bronze/silver
files are empty Paths-only placeholders; they remain covered by the authored
record decoder but do not form runnable native worlds. The C oracle includes
the production deformable translation unit unchanged, renaming only the resource
constructor symbol so a wrapper can capture its actual input/output during real
host map startup. No constructor formula is copied into the oracle. Inputs to
Rust are original authored records/resources plus each C boundary's RNG seed;
all compared persistent component values, vertex coordinates, geometry alias relations
and outgoing RNG match. Tests also install the resulting components in Rust.
This is class-construction boundary parity, not complete Rust startup/RNG replay
through the other classes; it does not qualify an independent moving game.

After the arena/record-layout change, the connected physics test still matches
1,254 records / 8,716,157 words (37 real hits, 33 later sampling borrows), and
6,144 deformation cases plus both 6,144-case contact suites pass. Workspace tests,
native/all-target Clippy, Wasm game Clippy and unchanged-production-C verification
pass. Renderer behavior is unchanged. New geometry/records are original resource
and dynamic component storage, not a new spatial acceleration cache; full live
Wasm memory accounting and whole-frame profiling remain future work.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scene_startup.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/scene_startup_reference.exe
$env:TANKRACER_SCENE_STARTUP_REFERENCE=(Resolve-Path build/native/scene_startup_reference.exe).Path
cargo test -p tankracer-game --test scene_startup_reference -- --ignored --nocapture
```

Next: common Obstacle parameters and other class construction, named entity
resolution and world membership/scheduler, then the remaining vehicle prefix and
after-pose operations. M3 remains open until an independently initialized moving
proxy runs; neither this constructor nor the connected phase alone closes it.


## General Obstacle parent components and fragment construction

`obstacle_startup::Obstacle::build_components` builds parent-owned collider,
visibility, type/explicit solidity precedence, bounds/margin, activation speed,
auxiliary parameters and 3D/sprite/animation fragment state from authored records.
Subclass scalar defaults are explicit inputs, including the sort bias (Bounce
starts at -8); they are not inferred as zero or read from an original-memory
arena in production. The type catalogue is real OBPARAMS data. Powerup's 410730
early return skips generic lap rules; its pre-record method and Mine's request
shared models, not level models. Current standard pickup cache priming follows
the actual bootstrap's cup/mode gates.

Auxiliary state retains all six gravity/pair defaults and overrides. Mesh parts
consume six RNG values and sprite parts two, even for a missing model. Typed
fragment state retains matrices, explicit ambient matrix tails, positions,
velocity/spin factors, angular rates, flags, offsets, ground height, gravity,
friction and direction. Terry offsets use binary32 conversions and preserve
both orientation branches. Sprite/animation materials are logical asset-owner
requests, without WebGL objects in simulation. Their request order is tested.

`install_in_grid` moves common collider/properties into the existing collision
world and preserves metadata/effect ownership in `PlacedObstacle`. It is only
for classes using the ordinary 41C230 enter method; special classes have their
own placement. Activation speed (+94) becomes collision Object.threshold;
subclass impact threshold (+7C) stays distinct. Among captured constructors,
1,949 Obstacle/Fall/Bounce/Spin placements match the native packed cell, including
lap-delayed absence. This validates data placement, not complete per-cell order
through every other class or an executing unified method table.

**2,937 common parent component outputs match across 22 nonempty map/CUP files**:
384 auxiliary headers, 152 debris groups, 837 parts (including 105 absent mesh
resources and 331 Terry parts), nine sprite groups and 58 animations. Shared RNG,
collider/scalar values, local/world transforms, fragment geometry, logical
material requests, UV/color/config fields and grid-cell placement match. C input
provides four subclass defaults, each constructor boundary's RNG and external
material results. The Rust build uses original authored archives for all other
inputs. This is parent-component boundary qualification, not complete subclass
construction or startup RNG replay. Enter/leave/final virtual methods, the
notification scheduler, fragment activation/ticks and named attachments remain
required; build_components explicitly leaves these to the world/class owner.

### Original animation constants omitted by the C bootstrap

Read-only inspection of `../tankracer/tankrace.exe` confirmed SHA256
`e8c4f12dd3caac55c4b1b29536475b58222dc44de82e008cae0e4a9c7fd9b9bb` and original
strings at 52E1C8/4/0: NONE/SUB/ADD, mapping to -1/0/1. The current C bootstrap
does not initialize them. Rust uses those original definitions; the constructor
oracle explicitly supplies these three immutable constants before invoking
unchanged C, rather than copying the C bootstrap omission/entity-ID fallback.
Unknown text still retains the original explicit entity fallback. Production C
and the reference site are unchanged. This is blend-mode *data* qualification;
these new animations are not yet drawn by the Rust browser and no new pixel/GPU
claim is made.

The gold ENGLAND resources also contain legacy N/D tokens inside shatter lists.
Inspection of original 41EEF3..41F0A1 confirms the same three-token piece parsing
as C. Missing names therefore retain null model references while numeric reads
and RNG continue; the port does not silently skip or repair those records.

Workspace tests, native/all-target and Wasm game Clippy, unchanged-production-C
verification, all 19 preset comparisons, the 50-file world record suite and the
154-Crush-construction regression pass. No new spatial precomputation or
whole-frame performance claim is introduced; full live Wasm budget accounting
remains pending. M3 is still open and the moving Rust game is not running.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_obstacle_startup.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/obstacle_startup_reference.exe
$env:TANKRACER_OBSTACLE_STARTUP_REFERENCE=(Resolve-Path build/native/obstacle_startup_reference.exe).Path
cargo test -p tankracer-game --test obstacle_startup_reference -- --ignored --nocapture
```

The oracle includes the production parent translation unit unchanged, wrapping
its registry entry to capture real map startup. It does not duplicate constructor
formulas. Next: concrete subclass initialization and lifecycle/method bindings,
named entity resolution and complete world startup, then remaining vehicle
prefix/after-pose operations and the independent moving proxy.

## Live scheduler and named attachment bindings

`lifecycle::Ticks` now runs callbacks as well as maintaining membership. The
same generic implementation accepts dense `ObstacleId` or global `EntityId`;
existing breakable physics still owns its original flags, without another copy
for queues. A pass saves the successor before dispatch and follows the live
cursor afterward. Disabling that successor advances the cursor before removal,
even if removal then exceeds its work budget. Appending from the final callback
waits until the next pass. Method failures retain cursor/slot/writes as in C.
The unchanged-membership path is linear; it repairs its Vec position only when
callbacks change membership.

`lifecycle::Queues` implements unique LIFO notifications/destruction sharing
bit 32 in those same flags. In particular, a pending stop suppresses a later
destroy request. Cancellation clears that bit before its potentially failing
search. Draining consumes an item before calling stop, then destruction if
requested; notifications finish before the destruction phase. Notifications
queued during destruction wait for the next drain. Each terminal empty pop also
spends work budget. VecDeque gives constant-time head push/pop; class callbacks
are still responsible for their own flag changes, destructor and ID release.

**585 scheduler scenarios / 243,846 semantic words match production C**, with
4,070 callbacks, 176 record releases and 2,167 error prefixes. The oracle calls
real `frame_entities`/`frame_pool` code. Scripts supply callbacks, including
membership/queue mutations, positive/negative failures and destructor/release
order; they do not substitute for the pending real Rust class methods. Random
scripts target live records; stale callbacks after ID release and allocator/I/O
faults are outside this qualification.

`symbols::Symbols` retains stable logical name handles and separate class/entity
namespaces. Names fold ASCII case, retain their first spelling, stop at embedded
NUL, and preserve the anonymous nil symbol. Native equality checks at most 32
characters and then requires termination: names longer than 32 bytes deliberately
receive a fresh handle on every intern, including exact repeats. Interning a
forward reference does **not** reserve an entity ID. Session clearing removes
entity bindings while retaining handles and classes (unit-qualified).

**224 symbol operations / 823,548 words match C**, including empty/nil names,
non-ASCII rejection, embedded NUL, casefolding, 31/32/33-byte boundaries and
1,023-byte names. The oracle calls real symbol initialization/interning; logical
entity/class values are boundary bindings, not class registration or object
allocation. Symbols are host-owned data, without a guest memory arena.

Breakable attachments now hold direct IDs or symbol handles, resolving each
immediately before dispatch. An earlier callback can rebind a later attachment;
the target is then checked against live collision components. The expanded real
hit/collision oracle matches **4,096 records / 881,768 words**, including 441
actual binding changes, 535 callbacks, 2,025 collisions and 132 error prefixes.
The existing membership oracle also matches 8,192 records / 1,923,606 words.
Unbound/missing-name and retired-component references remain absent in this
typed adapter; native invalid-pointer behavior from a malformed unresolved
attachment is not qualified. Authored attachment parsing, reserved/duplicate
definition policy and complete factory binding are still pending.

Workspace tests, native/all-target and Wasm game Clippy, unchanged-C verification
and the release browser build pass. No renderer/state changes or new GPU claims.
The browser still shows the static world; these modules are not connected to a
complete live Rust world. No new per-map acceleration data or whole-frame speed
claim. Next: canonical entity allocation/ID reuse, class methods and authored
attachments/world ticks, then vehicle prefix/after-pose and the moving proxy.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_lifecycle.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/lifecycle_reference.exe
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_symbols.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/symbols_reference.exe
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_breakable.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/breakable_reference.exe
$env:TANKRACER_LIFECYCLE_REFERENCE=(Resolve-Path build/native/lifecycle_reference.exe).Path
$env:TANKRACER_SYMBOLS_REFERENCE=(Resolve-Path build/native/symbols_reference.exe).Path
$env:TANKRACER_BREAKABLE_REFERENCE=(Resolve-Path build/native/breakable_reference.exe).Path
$env:TANKRACER_MEMBERSHIP_REFERENCE=(Resolve-Path build/native/membership_reference.exe).Path
cargo test -p tankracer-game --test lifecycle_reference --test symbols_reference --test breakable_reference --test membership_reference --locked -- --ignored --nocapture
```

## Entity identity, factory and shared dispatch context

`entities::Entities<T>` owns typed class records and Copy component handles.
ID zero is reserved; automatic allocation follows the native free chain, release
prepends IDs, and requested IDs unlink their first free occurrence. There are no
generation counters: an old reference observes a reused ID. Requested occupied
IDs replace their table binding and still increase the original record counter;
dropping a Copy handle does not implicitly clean up class-owned components.
Record accounting happens after both destruction methods succeed, independently
of ID release. Same-capacity reset retains record bindings and resets free links;
the session owner must perform the original teardown before reinitialization.

Growth preserves the native 128-slot policy. If the old free chain is nonempty,
the new free chain is not appended to it. Those new slots remain available to
explicit requests, while exhausting the old chain causes another growth. The
free-head/high-water behavior, stale free-link words and sequential requested-ID
advance are qualified, including failed constructors. No original addresses or
raw memory arena initialize Rust records.

`entity_factory::State` owns entities, symbols, tick/queue state and current
method slot. Its borrowed `Context` is shared by synchronous recursive class
calls. Creation writes class then dispatches allocation and initialization;
reserved filling rejects missing/in-use records before writing class. Failure
retains prior writes and the exceptional current slot. Destruction dispatches
destructor then ID-release before decrementing the record count. Tick/drain call
these same class tables, with callbacks mutating the live scheduler. Neither
allocation nor generic destruction invents a default component or removes
class-owned memberships/resources implicitly.

Eight real common bindings are exposed as `BaseMethod`: identity, zero, one,
enable/disable tick, queue destruction, release ID and detach record. These map
to the native base routines; a future class catalogue maps logical MethodIds to
them. Component allocation, initialization and specific class behavior still
need their actual Rust implementations.

**155 scenarios / 1,718 operations / 599,859 words match production C**, with
1,292 class callbacks and 256 error prefixes. The oracle executes real entity
table, factory, dispatch, tick/queue and native base routines. It covers free-ID
reuse, multiple growths, requested overwrite, unlink traversal limits, same/new
capacity reset, reserved/duplicate fill, null/out-of-range methods, positive and
negative callback failures, recursive construction, live tick changes and
destruction-phase notifications. Snapshots compare every logical record/free
link, counters, flags, queues, cursor, slots, component identities and callback
trace. Class allocation/initialization callbacks are scripted boundaries here;
this does not qualify actual Obstacle/Crush/Tank class startup.

`Context::define` connects names to the same factory: publish after successful
allocation/initialization, reuse only a blank reserved record, reject ordinary
duplicate definitions, and keep nil anonymous. Unit tests cover forward names,
reserved fill, duplicates, nil, binding visibility after ID reuse and failed
constructor/release prefixes. These definition tests are source-guided; the full
native world reader/class catalogue and authored attachment reader remain open.

Qualification assumes valid records/class handles and a functioning host
allocator. Raw C table-copy interruption/OOM/memory-fault prefixes and invalid
signed IDs are outside this typed simulation boundary. Rust retains requested
growth/unlink traversal budgets but does not recreate partially copied guest
memory. C's operational copy guard is not an original game timestep rule.

Workspace tests, all-target/native and Wasm game Clippy, source verification,
the lifecycle/symbol/breakable C regressions and release browser build pass.
No renderer changes or new GPU/whole-frame performance claims. No map
precomputation added; entity/component state is dynamic memory. The browser is
still static, and the moving Rust vehicle is not running. Next: concrete common
Obstacle/Crush lifecycle and record retirement, authored class/attachment
factories, then complete vehicle prefix/after-pose and independent world ticks.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_entity_factory.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/entity_factory_reference.exe
$env:TANKRACER_ENTITY_FACTORY_REFERENCE=(Resolve-Path build/native/entity_factory_reference.exe).Path
cargo test -p tankracer-game --test entity_factory_reference --locked -- --ignored --nocapture
```

## Concrete Crush records and lifecycle components

`crush` implements the actual record tick and its entity-table binding. Blink
states 0/1 subtract dt only while timer > dt; toggling adds 500 to the old timer
without subtracting dt. State 2 compares the first color channel as **signed i8**,
then retires or subtracts dt into all three bytes. Unknown states are not counted
as retired. Siren retirement uses the strict 11,026 boundary and wrapping dt*8;
a nonzero overshoot below the boundary clears on the following update (an exact
zero can stop immediately). Record/flag/queue writes
retain their original ordering on traversal or notification failures.

`deformation::State::records_attached` represents the live buffer independently
of its declared record count. Tick detaches it before enqueueing stop. Sampling
then skips retained records while still reading the same current mesh; drawing
must observe this flag too. Sampling, record ticking and constructor data use one
authoritative buffer. The siren timer moved from duplicated startup metadata
into the surface runtime component. Highlight mode is true only for mode == 1.

Crush enter appends collision/drawing memberships before requesting activation;
the class owner must then call the real activation interface. Leave removes first
occurrences in source order and requests deferred stop only for an attached
buffer. Its result is notification presence, rather than a guest node pointer;
node-address identity is not part of this typed interface. Owned destruction
detaches vertex flags, releases a private mesh and detaches records **before**
the parent-class method. The common Obstacle/Model parent destructor is still
pending; `destroy_owned` deliberately exposes this prefix, not a completed
destructor. Mesh retirement does not renumber handles or release shared/named
geometry; live getters reject retired private geometry.

One redundant camera query was removed: in the valid native path its only effect
is conditionally writing zero to the audio-channel field, which is already zero
when the conditional write is skipped. Rust clears that field directly. This
preserves semantic output for both near/far cameras; C read-fault ordering is
outside the typed-resource boundary. No whole-frame speedup claim is made.

**178 scenarios / 840 updates / 52,350 semantic words match production C**:
140 error prefixes, 218 detached-buffer observations and 445 callbacks. Tests
combine sampling, tick, activation, deferred stop/drain, highlight and owned
cleanup. They cover signed-byte fading, blink/wrap sequences, siren overshoot,
duplicate memberships, partial removal, notification failures, activation/parent
failures and private/shared destruction. The real entity scheduler dispatches
the real Crush tick binding. Parent callbacks are explicit scripted boundaries;
leave's node return and set-flag's object return are normalized to presence or
logical identity. No pointer-numeric equivalence is claimed.

The real archive constructor oracle now also compares **154 constructed Crush
objects / 28 glow records / 302 record ticks**, with all 154 buffers detached.
It activates each authored record through the real sampler, performs up to two
dt=1500 boundary updates to cross retirement/siren branches, and samples again.
These are deliberate short module sequences, not normal-duration race frames.
Initial surrounding flags/notifications remain C boundary inputs; actual surface
data is constructed from archives in Rust. C alone uses a copied arena plus a
disjoint allocation range above the declared original 8 MiB, leaving native
startup and its allocator intact. Rust receives semantic fields, never that
arena. Constructor-only placement coverage is retained separately.

Regression: connected vehicle 1,254 records / 8,716,157 words; both 6,144-record
contact suites; 6,144 deformation records; and 2,937 parent constructions match.
Workspace tests, native/all-target and Wasm game Clippy, unchanged-C verification
and release browser build pass. No renderer or GL pipeline state changed;
no new GPU claim.
No map acceleration data added; full live Wasm budget aggregation stays pending.

M3 remains open. Concrete activation/parent bindings, common Obstacle methods,
authored class/attachment startup, full shared RNG replay and complete vehicle
prefix/after-pose still need connecting before the moving proxy runs independently.
Do not treat the new enter/destructor prefixes as a complete game lifecycle.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_crush.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/crush_reference.exe
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scene_startup.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/scene_startup_reference.exe
$env:TANKRACER_CRUSH_REFERENCE=(Resolve-Path build/native/crush_reference.exe).Path
$env:TANKRACER_SCENE_STARTUP_REFERENCE=(Resolve-Path build/native/scene_startup_reference.exe).Path
cargo test -p tankracer-game --test crush_reference --test scene_startup_reference --locked -- --ignored --nocapture
```

## Common scene-object lifecycle and real parent dispatch

`scene_objects::Scene` now owns ordinary/special draw memberships alongside the
existing collision world and deformable/mesh components. It implements the
shared entity factory's method interface: Obstacle initialize/enter/leave,
mode switch, position/grid update, collision membership, deactivation, owned
cleanup and destruction; Crush enter/leave/highlight/initialize/tick/destruction
use the same scheduler, queues and current method slot. Component installation
is separate from entry, so class methods determine when membership changes.
The previous data-placement convenience methods keep their old behavior.

Direct parent calls reuse the live interface slot without saving/restoring it.
Their outputs commit only on success. GenObst's inherited initialize calls World
to install the identity transform, then sets sort position to -8; ordinary
Obstacle subsequently sets its cell to -1. Crush overrides initialize without
calling those parents, retaining its zero-initialized cell/transform until the
authored reader supplies its pose. Deactivation requests destruction through the
real parent before removing spatial/draw memberships. A pending stop shares the
queue flag and can suppress that destruction request, as in C.

Crush destruction retires owned records/private geometry before the Obstacle
parent clears auxiliary/debris resources and removes grid membership; the generic
parent then releases object storage. ID detachment drops typed components without
renumbering other handles. Shared/named meshes survive. Private main-mesh release
also clears the drawable mesh handle. Host allocation/free failures and guest
pointer identities remain outside the typed contract: notification nodes become
presence, and returned object pointers become logical component identity.

**664 scenarios / 1,672 operations / 367,097 semantic words match production C**,
including 140 error prefixes and 176 retired observations. The new oracle calls
the actual `frame_native_dispatch`, native classes and inherited handlers; only
host allocation/free are supplied externally. It compares every intermediate
pose, cell, initialization scalar, retained Crush record, object lifetime,
current slot, flag, queue and all 64 grid lists. Scoped test slots exercise the
real bodies without claiming the complete original bootstrap catalogue.
Coverage includes parent out-of-range dispatch, duplicate memberships, partial
removals, all tested work-limit failures, private/shared shapes and short
entry/move/exit/tick/stop/destruction sequences. Unit tests additionally cover a
parent callback changing the live slot and failing after writing a private result.

Regression: the 178-scenario Crush suite, 154 authored Crush constructions / 302
runtime ticks, 2,937 authored Obstacle constructions, 155 entity-factory scenarios
and 1,254 connected vehicle records still match C. Workspace tests, native/Wasm
Clippy, unchanged-C verification and browser release build pass. The browser is
still the static scene (183,221 B Wasm); its game crate is not wired into the live
browser yet. No renderer/GL state changed, new GPU claim or map acceleration data.

Still missing: fresh class allocation and authored-reader binding through these
methods, subclass/attachment factories and auxiliary face/backup motion,
full shared-RNG world startup, and the vehicle prefix/after-pose integration.
Existing breakable collision callbacks still have their own context and must be
connected to the same entity scheduler/symbols rather than duplicated in a final
world owner. M3 remains open until the moving proxy actually runs independently.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scene_objects.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/scene_objects_reference.exe
$env:TANKRACER_SCENE_OBJECTS_REFERENCE=(Resolve-Path build/native/scene_objects_reference.exe).Path
cargo test -p tankracer-game --test scene_objects_reference --locked -- --ignored --nocapture
```

## Fresh ordinary Obstacle factory and authored reader

`scene_loading::Registry` binds World -> TrWorld -> GenObst -> Obstacle using
named Rust interface slots and immutable method tables. `Loader` creates blank
typed components through the shared entity factory, runs actual inherited
initialization, publishes the authored name, loads the model/pose, and invokes
the real constructor chain. Its resource services own the model cache, original
archives, materials, terrain and shared RNG; no C arena initializes Rust objects.
Unsupported subclasses fail explicitly rather than inheriting ordinary behavior.

The ordering is now executable: activate first, apply common visibility/sort/
solidity, append/remove the temporary ordinary-draw membership, choose lap entry
or exit, then build parent-owned bounds/presets/auxiliary/debris. The existing
qualified resource builder accepts the already-decided visibility so it cannot
draw the lap RNG twice. The live constructor preserves the cell established by
the earlier enter/leave call. Its final virtual method requests deferred stop.
That last call was missing from component-only construction: the first live
comparison found native flags 56 versus Rust 24. Installing the real notification
binding corrected the flags and LIFO stop order.

The fresh World oracle uses production static/bootstrap functions, the complete
C class catalogue, the actual FSTPC reader, native dispatcher and a new entity
table. **24 scripts / 144 constructed objects / 96 lifecycle checkpoints /
19,932 semantic words match**, including ID-table growth, anonymous names,
static/dynamic allocation, varied default/overridden visibility, wrapping scalar
widths, lap -99 shared RNG, delayed entry/removal, deferred stop, deactivation
and destruction. A single RNG seed starts each script; no per-object snapshot or
seed resets are supplied. Nil-shape scripts intentionally isolate those rules
from resource I/O. All 64 grid lists and all draw/lap/tick/queue orders are checked.

The OS03 real-map constructor oracle additionally filters memberships to the
ordinary objects seen so far. **All 56 authored ordinary Obstacle records** across
the 22 nonempty map/CUP files pass through fresh Rust allocation/initialization/
reader bindings, with geometry, scalars, auxiliary/debris, resource request order,
RNG and filtered live memberships matching. Their IDs and entering RNG seeds
remain boundaries from the surrounding unported world. The original **2,937
parent constructions** and **1,949 placements** remain qualified separately;
those include subclasses and must not be presented as 2,937 complete factories.

A native unit test also verifies that unsupported classes allocate nothing and
that a bad model retains the created/name-bound object at the failed reader slot,
before activation or pose writes; normal destruction can retire that object.
Typed resource failures after later construction stages are not a general proof
of native allocator/read-fault prefix equivalence.

Regression: shared lifecycle, entity factory, Crush construction/runtime and
connected vehicle oracles pass, as do workspace tests, native/Wasm Clippy,
unchanged-C verification and browser build. No renderer or map acceleration data
changed. Browser Wasm stays 183,221 B and still renders the static scene only.

Next: actual Crush and Fall-family allocation/readers, attachment binding and
runtime methods on this same owner; then whole-world startup/RNG and the complete
vehicle update. Generic C destruction leaves lap-list references for its world
owner to retire; plugin teardown must clean its owned lap entries too. M3 remains
open, and this loader does not yet accept the complete object file.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scene_loading.c build/native/libtankracer_core.a -lm -o build/native/scene_loading_reference.exe
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_obstacle_startup.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/obstacle_startup_reference.exe
$env:TANKRACER_SCENE_LOADING_REFERENCE=(Resolve-Path build/native/scene_loading_reference.exe).Path
$env:TANKRACER_OBSTACLE_STARTUP_REFERENCE=(Resolve-Path build/native/obstacle_startup_reference.exe).Path
cargo test -p tankracer-game --test scene_loading_reference --test obstacle_startup_reference --locked -- --ignored --nocapture
```

## Fresh Crush reader on the shared scene owner

The registry now binds Crush allocation/initialize/read/enter/leave/highlight/
tick/destruction to the same factory and scene components as ordinary Obstacle.
Crush initialization has an explicit scalar-only phase: it sets its defaults
without calling the ordinary parent, and has neither geometry nor a record
buffer yet. A small transient pending list holds that state until construction
finishes; no fake/empty mesh is inserted. Early reader failure remains
destructible, including a highlight call before a model has loaded.

The inherited constructor actually activates the object and applies lap rules
before its buffers exist. Early Crush entry therefore appends its collision/
special-draw memberships but does not request a second activation. The parent
also requests deferred stop. A live glow buffer cancels that notification only
for immediate-entry objects; delayed objects retain it. The subclass then owns
its current mutable mesh, records, random vertex flags and surface-query state.
Initialized defaults and optional private-mesh replacement remain separate from
parent-owned effects and the original cell value.

The specialized builder was split into parent input and subclass completion.
The live reader reuses the already-constructed parent pose, geometry, bounds,
weight, sort and lap decisions, consuming neither lap RNG nor parent effects a
second time. The data-only constructor convenience path uses the same subclass
completion and retains its previous qualification.

The SC03 oracle additionally captures real constructor flags/cells and filtered
draw/deformable/lap/tick/queue/grid memberships. **All 154 authored Crush records
across 22 nonempty map/CUP files** now pass through fresh Rust factory/reader
bindings and match those outputs, as well as geometry sharing, initialization
scalars, terrain heights, records, vertex flags and RNG consumption. Their IDs
and entering RNG seeds remain explicit boundaries from the surrounding
unported constructors; this does not prove complete-world startup/RNG replay.

Existing short sampler/retirement sequences also run through the live owner:
**302 bound tick calls / 154 detached buffers**, reusing the same 28 authored glow
records. After each C-qualified checkpoint the live component/flags/filtered
notifications agree. Small component/scheduler clones restore constructor state
only inside the test so subsequent startup-membership comparisons stay valid.
The complete live leave -> stop -> deactivate -> destroy path retires all 154
objects; private meshes disappear and shared/named meshes remain accessible.
The older 178-scenario Crush and 664-scenario common-lifecycle error-prefix suites
also pass after the pending-state/record-presence API changes.

Other regression: 24 fresh ordinary scripts, 56 authored ordinary factories,
2,937 parent constructions, entity factory and connected vehicle oracles,
workspace tests, native/Wasm Clippy, unchanged-C verification and browser build
pass. No renderer state or map acceleration data changed. The browser remains
the static 183,221 B Wasm build; the live game crate is not connected there yet.

Next: Fall-family/subclass readers and attachments, shared breakable scheduling,
then whole-world startup and vehicle prefix/after-pose. M3 remains open. Full
native allocator/read-fault prefix fidelity during partial resource construction
is outside the typed resource boundary; no such new claim is made.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scene_startup.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/scene_startup_reference.exe
$env:TANKRACER_SCENE_STARTUP_REFERENCE=(Resolve-Path build/native/scene_startup_reference.exe).Path
cargo test -p tankracer-game --test scene_startup_reference --locked -- --ignored --nocapture
```

## Fall construction, motion and named attachments

The fresh class registry now accepts single-player Fall. Its initializer inherits
ordinary Obstacle; its actual constructor runs that parent, clears activity,
identifies canonical sign meshes by handle, saves sign restoration matrices and
interns ordered attachment names. Interning a forward name does not allocate an
entity: bindings resolve against the shared symbol table later. The parameter
lexer now exposes the produced final token separately from the native `more`
flag, because attachment-name reads ignore that flag. This preserves names at
the very end of a group. Destruction releases attachments before the actual
Obstacle parent and ID detachment retires the typed Fall component.

`assets::object_catalog` also decodes six Fall class settings from OBPARAMS,
including wrapping degrees * 182 for the squash angle. OC02 compares those
settings and all 19 object presets with real C startup. Runtime scenes retain an
`Arc<TerrainData>` shared with the loader/contact owner; the terrain graph is not
cloned. Canonical sign preparation is an explicit level-bootstrap operation.

The pure `fall` module implements impact-following yaw, direction/flip choices,
slope lookup, gravity, pitch, rebound, settling and stop timing. Stop removes
grid memberships and enqueues its notification **before** the final matrix
rotation. Wrapping products, signed shifts, INT_MIN behavior and the source's
16380/16384 distinction are retained. `scene_objects/falling` binds that motion
to the authoritative collider, Fall state, memberships and shared entity queues;
it does not introduce another scheduler. Auxiliary activation is an explicit
live mode word, independent of private-mesh ownership.

**148 branch/short-sequence scenarios / 392 updates / 33,545 semantic words**
match unchanged C motion, including 16 error prefixes, 36 terrain requests and
150 stopped observations. The kernel oracle injects only the height result/error
boundary; actual C matrices, grid removal and notification methods still run.
It covers cardinal/threshold directions, reverse/flip flags, wrapping arithmetic,
small budgets and error timing. It does not claim to run shatter methods.

OS05 additionally qualifies **1,252 authored Fall factories**, their **11 named
attachments**, sign backups and filtered live memberships across all 22 nonempty
map/CUP files. Along with the existing 56 ordinary objects, 1,308 objects now use
the real Rust reader/factory. Existing 2,937 parent/1,949 placement checks remain.
**22 actual scene-bound rebound/stop updates** compare matrices, activity, motion,
base flags, filtered grid and notification state with C on real authored objects.
C alone uses a copied arena for that short update, leaving startup unchanged;
Rust uses its own typed components and only test-local state saves/restores.
Per-object IDs/entering seeds remain surrounding-world boundaries in this suite.

The fresh World script suite now mixes ordinary and Fall classes: **24 scripts /
144 objects / 96 lifecycle checkpoints / 22,740 words** match from a fresh table
and one shared RNG seed per script. It checks forward attachment resolution,
anonymous objects, sign membership and complete stop/deactivation/destruction.
Tagged nil/class union payloads are compared as logical entity bindings, not
guest addresses. A normal native unit test checks forward references and final
tokens without a C toolchain.

Regression: Crush construction/302 bound ticks, common lifecycle, entity factory
and connected vehicle oracles pass; workspace tests, native/Wasm Clippy,
unchanged-C verification and browser build pass. No renderer, new acceleration
data or whole-frame performance claim. Wasm remains the static 183,221 B build.

**Still incomplete:** the old breakable-hit path must borrow these same Fall
components, symbols and entity scheduler rather than its isolated Store. Active
auxiliary/debris virtual updates are explicit unimplemented methods and fail
instead of silently dropping effects. Bounce/Spin and remaining readers are not
accepted yet. Network-only Fall gates are outside the single-player scope. World/
plugin teardown must retire owned sign/lap references as well as entities.
Next connect the shared collision owner, then auxiliary/debris and remaining
class bindings before whole-world/vehicle integration. M3 remains open.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_fall.c build/native/libtankracer_core.a -lm -o build/native/fall_reference.exe
$env:TANKRACER_FALL_REFERENCE=(Resolve-Path build/native/fall_reference.exe).Path
cargo test -p tankracer-game --test fall_reference --locked -- --ignored --nocapture
```

Rebuild `reference_object_catalog.c`, `reference_obstacle_startup.c` and
`reference_scene_loading.c` with their documented link libraries for the updated
OC02/OS05/SL02 protocols before rerunning those integration tests.

## Shared collision activation and Fall update ownership

The live scene now separates collision `World` from authoritative `Components`.
`scene_objects::View` borrows both for the same lifecycle methods; normal Scene
invocation delegates to that view. The loader and tests name the component owner
explicitly rather than using field forwarding or hidden interior mutability.
`entity_factory::State::split` borrows identity/symbols/ticks/queues separately
from the actual current dispatch slot. Collision and vehicle globals are borrowed
RNG/slot references, so an eventual runtime can use the real factory slot and
shared seed without copy-in/copy-out at a callback boundary.

`breakable::hit` contains the one qualified 40A3E0 algorithm. Its owner supplies
borrowed motion fields and live resource metadata, ordered Arc-backed attachment
bindings and immediate methods. The former Store is isolated in a qualification
adapter; it is not copied into the live scene. `scene_objects::collision` instead
writes the existing Fall motion, object effect flags, mesh/cell memberships and
factory scheduler. Each named attachment resolves against the live shared symbol
and entity tables immediately before its call, now with O(1) entity lookup.
Callbacks can rebind later names and change RNG, flags and vehicle state in place.

Activation uses the real factory BaseMethod::EnableTick with EntityId indexing.
Physics and entity dispatch retain their distinct result/error/slot behavior.
The registry has explicit auxiliary/debris contact interfaces as well as update
interfaces: unported methods return -5, never a successful null method. Host sound
and feedback remain explicit services. Optional impact sound handles are live
object components; the preserved C loader disables resource creation and leaves
these absent. This increment does not claim complete audio loading.

Qualification:

- **4,096 short-sequence records / 881,768 semantic words** run through both the
  standalone qualification owner and the actual scene/factory owner against C.
  Includes 132 error prefixes, 535 callbacks, 441 dynamic name rebindings,
  1,276 active-list observations and 2,025 collision hits. The scene fixture is
  constructed once per sequence, not synchronized from the other owner after
  callbacks or frames. Tick flags/order are compared through actual entity IDs;
  the real factory queues are checked as well.
- **OS06** extends the real map constructor suite with **22 authored object
  collision-to-Fall sequences / 132 checkpoints**: native hit dispatch, four
  updates (16/33/40/1000 time units), then a repeated hit. Matrices, motion,
  last-vehicle identity, activity, flags, RNG, current slot, ordered ticks,
  notifications and filtered grid memberships match. Peer vehicle/device values
  are explicit inputs; terrain is the real shared map graph. These cases select
  objects without auxiliary/debris/attachments; other hit branches are covered
  by the owner/error suite above, not claimed as ported effect implementations.
  Native scratch arenas and Rust state saves/restores exist only in the test.
- The prior 1,252 Fall/56 ordinary/154 Crush factories, 22 bound Fall stop ticks,
  302 bound Crush ticks, common lifecycle, factory and connected-vehicle suites
  pass. Workspace tests, native and Wasm Clippy, source preservation verification
  and release browser build pass. The C runtime's 244 files remain unchanged.

No renderer state, acceleration data or measured frame-time change. The browser
remains the static 183,221 B Wasm build; the game crate still needs runtime wiring.
This completes shared breakable activation, not M3. Next implement actual active
auxiliary/debris contact/update bindings on this owner, then remaining readers
and vehicle prefix/after-pose plus scheduler -> independent moving proxy.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_obstacle_startup.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/obstacle_startup_reference.exe
$env:TANKRACER_OBSTACLE_STARTUP_REFERENCE=(Resolve-Path build/native/obstacle_startup_reference.exe).Path
$env:TANKRACER_BREAKABLE_REFERENCE=(Resolve-Path build/native/breakable_reference.exe).Path
cargo test -p tankracer-game --test obstacle_startup_reference --test breakable_reference --locked -- --ignored --nocapture
```

## Live debris activation, motion and animation

`shatter` implements the resource-backed 41CF80/41C980/41CEF0 paths. It operates
on the actual constructed Debris/Piece/Animation data, preserving independent
piece state, source model radii, sprite radii, matrices and translation tails.
Activation commits active/retired flags before immediate blast callbacks, then
consumes the shared RNG for animation phase, scattering and angular impulse.
The slower-explode branch uses the original unscaled temporary velocity for
angular magnitude even though the stored linear velocity has been reduced.
Terry-style pieces, directional scatter and sprite spin keep their own paths.

Updates implement gravity, friction, rotations, ground bounce, random re-launch,
rolling and retirement, including signed wrapping/INT_MIN and angle errors.
Animation uses a 16-bit timer, advances at most one cell per call, discards timer
excess, retains byte UV wrapping and updates cycle/growth at the original point.
The resource component now retains the animation's 16-bit rotation word.

The registry's DEBRIS_CONTACT binding executes through the live collision owner,
removing ordinary drawing membership and appending unique special membership
only after the piece updates succeed. Native list search budgets include the
terminal sentinel; a full unsuccessful search can therefore fail after all
piece/RNG writes have committed. DEBRIS_UPDATE is implemented by the same scene
view used by Fall's virtual tail calls and deactivates through the real shared
factory/parent/queue path when every piece and animation has finished.

`Scene::updates(&mut random)` supplies a simulation-capable view. Nested class
calls use that very RNG reference, not an extra stream stored in a module.
Lifecycle-only views remain suitable for construction/destruction and reject
RNG-dependent ticks explicitly. The scene's frame-group value is an explicit
simulation input for the original periodic re-launch branch.

Blast eligibility checks preserve strict X/Z boundaries, wrapping distances,
protection timing and all nine ordered slots. Each slot is read again just
before its callback; earlier impulses may change later targets. The vehicle
impulse method is still an explicit **unported game binding**, not a device
substitution: missing target/impulse bindings fail -5. The native qualification
injects those pair-method callbacks; it does not claim the player's full blast
response is ported. Auxiliary mesh-break methods remain unimplemented as well.

**156 scenarios / 642 operations / 116,051 semantic words** match unchanged C
through the actual shared scene/factory owner. The SH01 oracle starts from fresh
C bootstrap/classes/World loading and replaces only typed resource fixtures and
vehicle input values. Tests cover direct activation/update, Fall virtual tail
updates, ordinary breakable hit -> real debris activation -> actual scheduler
passes, repeated hits, callback mutations/failures and short motion sequences.
Snapshots include Fall state/matrices, every fragment/animation field, shared
RNG/current slot, peer writes, scheduler cursor/flags/order, drawing membership,
queues and the full collision grid. There are **30 error prefixes**, 72 retired
observations and 30 blast-callback observations. A normal unit test checks that
frame updates require the shared stream and missing game bindings retain the
already-committed prefix rather than silently discarding an effect.

Regression: prior shared breakable, Fall kernel, authored ordinary/Fall/Crush
construction, fresh mixed scripts, scene lifecycle and connected vehicle suites
pass. Workspace tests, native/Wasm Clippy, browser release build and unchanged-C
verification pass. No rendering state or map acceleration data changed. Browser
Wasm stays 183,221 B/static; this is not a moving-vehicle milestone or a GPU
fragment-rendering claim. Next: auxiliary mesh break and remaining readers,
vehicle blast bindings, then complete vehicle prefix/after-pose and startup.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_shatter.c build/native/libtankracer_core.a -lm -o build/native/shatter_reference.exe
$env:TANKRACER_SHATTER_REFERENCE=(Resolve-Path build/native/shatter_reference.exe).Path
cargo test -p tankracer-game --test shatter_reference --locked -- --include-ignored --nocapture
```

## Live auxiliary mesh break

`mesh_break` now implements 41DC00/41D460 on private scene geometry. Model
resources retain the original XYZ face-center columns rather than deriving
centroids from vertices. `scene_meshes` binds/copies those columns and radius
alongside geometry; ticks borrow the same vertices, flags and centers that
render extraction will read. No per-update geometry copy occurs. Private mesh
retirement drops the center buffers too; shared bindings remain intact.

Activation uses local incoming velocity, the original push gain and two-draw
scatter: the FIRST product selects the sign branch and the SECOND is shifted.
Every face receives RNG/lifetime state before adjacent triangles are paired.
Pairing uses the last nonshared vertex, carries the prior tail between pairs,
and spends the same combined piece/pair work budget. Undefined degenerate tails
return -4 after the committed mode writes unless the caller supplies its actual
stack-tail input. The 2000-unit ordinary lifetime and supplied 1500-unit class
state are qualified; Pickup construction/capture is still separate work.

Motion preserves gravity **per update**, dt-scaled translation, quantized i16
vertices, Y/X/Z rotations and the source's unusual alternating friction/bounce
policy across all six linear/angular words. Paired heads move a fourth vertex,
skip their tail's tick and retire two faces. Completion clears drawing/active
state before immediate virtual deactivation, preserving queue/list errors and
partial writes. The collision radius cache changes to 5120 with the replacement
mesh and to zero when it clears, matching native queries/feedback of the current
mesh rather than retaining the original model's radius.

AUXILIARY_CONTACT is handled by the shared Collision owner; AUXILIARY_UPDATE is
handled by the Scene view and Fall's existing virtual tail. Resource resolution
is an explicit service, while binding/private cloning/state creation stay in
Rust. The default height service uses the real Rust terrain query; the focused
oracle injects only the height result/error boundary. Prepared-state fixtures
skip loading, while a fresh resource case executes native shape-cache lookup,
private clone and buffer creation. Allocator/read-fault prefix equivalence at
that typed resource boundary is not claimed.

Evidence:

- **43 scenarios / 159 operations / 29,608 semantic words / 14 error prefixes**
  match unchanged production C through the live owner. Includes fresh and
  prepared resources, generic breakable hit -> real mesh activation -> Fall
  virtual updates, wrap/shift boundaries, pairing/undefined tails, expiry and
  common completion/destruction queues. Snapshots include RNG/slots, height
  requests, motion, mesh/radius ownership, vertices, flags, centers and full grid.
- The extended **SM02** oracle compares all **435 shipped models / 11,713
  vertices / 10,430 faces**, now including every present authored center column.
- A normal native test resolves **49 map/replacement bindings across eight maps**
  and runs activation, movement and expiry while checking that shared geometry,
  flags and centers remain untouched. The INGAME pickup-shape preloads are the
  same previously qualified startup boundary, spanning all three cups in this
  resource test. This catches cconc-style cache hits before Level resolution;
  it does not introduce a cross-namespace fallback.
- Shared breakable, debris, Fall, authored ordinary/Fall/Crush startup, fresh
  mixed scripts, common lifecycle and connected-vehicle regressions pass.
  Workspace/native/Wasm checks, browser build and C preservation verification
  pass (97 modules / 244 unchanged C files). Current browser Wasm: **183,235 B**.

Still pending: the Pickup-owned backup capture/restore policy (and arbitrary
long-name/header aliases), other class readers, vehicle blast impulse bindings,
complete startup/shared RNG and vehicle prefix/after-pose. Future fragment render
extraction must use the live center columns as well as vertices, not the cached
billboard center in the immutable source model. No GPU fragment-rendering or
whole-frame performance claim. No new map acceleration data; the browser still
runs only the static world/free camera. M3 remains open.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_mesh_break.c build/native/libtankracer_core.a -lm -o build/native/mesh_break_reference.exe
$env:TANKRACER_MESH_BREAK_REFERENCE=(Resolve-Path build/native/mesh_break_reference.exe).Path
cargo test -p tankracer-game --test mesh_break_reference --locked -- --include-ignored --nocapture
```

Rebuild `reference_model.c` for SM02 before running the updated assets model suite.

Execution priority after mesh-break integration: the common collision/effect
owner is now connected, so the next slice prioritizes real vehicle construction,
prefix/after-pose and scheduler wiring into the independent moving proxy.
Remaining readers are brought forward when that slice requires them; other
actors/Pickup capture stay explicit later-stage work. This changes delivery
order, not the final single-player CUP scope or physics/RNG requirements.

## Active vehicle timer and ability prefix (2026-11-14)

`vehicle_prefix` implements the active 430A30 work before the first contact
read: player scenery-view update, alternating sign, countdown throttle/RPM,
auxiliary cache invalidation, boost decay, weapon spin, speed-pickup camera and
application, then temporary sidegrip expiry. This is a qualified part of M3;
the browser still has no independently constructed/moving Rust vehicle.

`spin` implements 4298E0/429A00, including original packed approach narrowing,
phase priority, strict angle limits, byte counters, masked color arithmetic,
three-draw color targets and hold/fade/restart behavior. `boost` implements
43F6A0/437BF0/43F720 and both camera variants. The application callback is
synchronous class dispatch: its known binding calls the real typed boost
method; null methods, another vehicle target, failed methods and changed
dispatch-slot lifetime are observable. There is no default successful callback.
The scene owner must still bind that callback to the runtime method table.

Application commit order differs by branch: nonplayer/preset paths mark applied
before dispatch, while the ordinary player camera marks it after success.
Shared boost UV state advances the authoritative RNG at most once per frame,
even when several effects use it. The player-view rotation consumes no RNG and
uses the original strict X/Z camera window and 32-frame normalization cadence.
Temporary expiry releases nine line handles in row/column order, preserving
duplicates and the already-qualified exhausted/overflow pool aliases.

`State::from_model` initializes live/restore grip from the resolved model.
`prepare_vehicle` derives dt/frame/player/active from the existing vehicle tick
and publishes boost charge, extra speed/power, pose timer and restored grip
directly into the qualified physics inputs. Committed prefixes remain published
on failure. It does not construct the vehicle, run the scheduler, or replace
the still-required full after-pose scene updates.

Verification:

- **VP01: 232 scenarios / 1,448 operations / 304,312 semantic words / 79 error
  prefixes** match production C. The oracle calls the actual helper entries or
  430A30 and stops at the first contact read, before any contact writes. It does
  not duplicate the C prefix. Comparisons include all owned prefix fields,
  auxiliary caches, view/camera state, UVs, feedback, shared RNG/dispatch slot,
  temporary slots and all line-release markers. Includes 8 self-target and 1
  peer-target successful applications, 16 rejected methods, null/out-of-range
  dispatch, invalid dt, wrapping arithmetic and bounded spin failures.
- The same cases run through `prepare_vehicle` and check the physics-input
  handoff, including failed prefixes. This is not a complete 430A30/after-pose
  or fresh vehicle-startup parity claim.
- Connected-physics regression remains exact: **1,254 records / 8,716,157 words**,
  with 603 peer writes. Workspace tests, native/Wasm Clippy, browser build and
  C preservation checks pass. Browser Wasm remains **183,235 B**; no rendering
  changes or extra map precomputation were introduced.

Next: construct the authoritative vehicle from resources/spawn data, bind this
prefix and the full after-pose operations into one scheduler, then run the
independent moving proxy. M3 remains open; a qualified prefix is not a car.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_prefix.c build/native/libtankracer_core.a -lm -o build/native/vehicle_prefix_reference.exe
$env:TANKRACER_VEHICLE_PREFIX_REFERENCE=(Resolve-Path build/native/vehicle_prefix_reference.exe).Path
cargo test -p tankracer-game --test vehicle_prefix_reference --locked -- --ignored --nocapture
```

## Fresh player dynamics construction (2026-11-14)

`vehicle_startup::Player::construct` owns the assembled `VehicleModel`, qualified
vehicle dynamics and timer/ability prefix. It initializes contact locals/corner
indices from the model, the original private shadow hull, drive/collision state,
body/reference basis and live/restore grip without importing a C frame snapshot.
The scene supplies logical entity/class identity, spawn and world settings.
Motion and startup share the same pose-environment conversion.

There are two real startup paths. An authored Player takes the world-file
transform through Tank construction; when no Player is declared,
`frame_game_load` creates one at zero with the identity basis. The shipped CUP
files tested here use that latter path. Both load the model (one pose update)
and explicitly update pose again. Two calls are necessary: ambient wobble can
advance physical Y twice, while the active-session flag suppresses it.

**VS01: 122 fresh constructions / 38,796 semantic words** match C. Coverage is
86 unmodified game startups across all eight maps, all three CUPs' valid track
variants, all fifteen vehicles and both supported modes, plus 36 controlled
world-input branches. Those branches include 24 nonzero-wobble cases, 12
active-session suppression cases and rotated/nonzero spawn inputs. The oracle
wraps the unchanged production pose implementation at the second player pose
call, after resource loading and before session ticks. It asserts that the hook
actually ran; an empty record stream cannot pass. Initial positions/matrices
are checked against authored input or the source's explicit zero-spawn path.
Snapshots compare motion/part poses, shadow vertices/radius, contacts, drive,
collision/prefix state and private gun geometry; they do not initialize Rust.

Qualification boundary: this builds fresh dynamics/resources, not the complete
Player lifecycle or a moving game. Entity allocation/names, memberships/ticks,
camera creation, route assignment, complete world startup/RNG and full after-
pose effects still need runtime bindings. Factory failure-prefix equivalence
and the full authored Player reader are not claimed by VS01. The controlled
branches alter inputs at the first pose boundary; they are not observations of
ordinary map spawn positions.

Prefix and connected-physics regressions remain exact (1,448 operations and
1,254 vehicle records respectively). Workspace tests, native/Wasm Clippy,
browser build and C preservation checks pass. Browser Wasm is still 183,235 B;
the Rust browser still renders the static scene/free camera. No renderer or
per-map acceleration data changed. M3 remains open.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_startup.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/vehicle_startup_reference.exe
$env:TANKRACER_VEHICLE_STARTUP_REFERENCE=(Resolve-Path build/native/vehicle_startup_reference.exe).Path
cargo test -p tankracer-game --test vehicle_startup_reference --locked -- --ignored --nocapture
```

## After-pose quad pool, ground lights and track marks (2026-11-14)

`effect_pool` now owns the source's quad allocate/update/replace/release flow and
visible-record iteration, alongside the existing line pool. Normal records stay
directly typed; two finite exceptional handles retain their observable aliases.
Quad -1 owns the preceding quad-sized record. Quad 160 aliases the two words
before line 0, all of line 0 and the position of line 1. Those words are the
existing exhausted-line color storage, so both directions share one owner.
Count equality, first-free scanning, no-free/count mismatch, arithmetic >>3,
idempotent release and reset-with-retained-payload behavior match production C.
Arbitrary out-of-bank indices and overlapping source-memory copies are not a
guest-memory API; callers pass typed values and generated/sentinel handles.

`vehicle_scene::lights` implements 4427E0 reverse/brake ground-light quads. It
preserves visibility/player/disabled/queued gates, strict speed and ground-height
limits, material checks, external basis, point winding and source style bytes.
Zero throttle deliberately releases/reallocates even while already braking;
optimizing that away would change allocation order shared with other effects.

`vehicle_scene::marks` implements 43A930, including pre-rescale lateral/slip
thresholds, contact/travel classification, retained endpoints across material
changes, end caps and the 68-handle FIFO. Full-ring retirement precedes new
allocation; exhausted allocations are not enqueued, while overflow handle 160
is. Both kernels expose ephemeral views of the canonical `AfterPose` physics
inputs. Their persistent states belong to the fresh `Player` owner, not a
separate mutable copy of vehicle pose/terrain/contact data. They consume no RNG.

Evidence:

- **VQ01: 168 scenarios / 1,176 operations / 4,096,176 words / 36 error
  prefixes** match unchanged production light/mark/pool entries. Includes 229
  light-state and 161 mark-state changes, 20 full-FIFO retirements, 14 wraps,
  56 direct normal allocations, 10 overflow and 6 exhausted allocations.
  Interleaved quad/line operations compare the complete quad and line banks,
  the preceding record, both aliases, counts, endpoint caches and FIFO state.
- **VS02** extends fresh construction parity to both newly owned components:
  **122 constructions / 50,142 words**. Rebuild the startup oracle for VS02.
- Prefix (1,448 operations) and connected physics (1,254 records / 8,716,157
  words) remain exact. Workspace/native/Wasm checks, browser build and C source
  preservation pass. Browser Wasm remains **183,235 B**. No renderer or extra
  per-map precomputation changed.

Still pending for the complete active after-pose chain: track spray, damage
smoke, shared debris, pickup shared fade, temporary effects and shield orbit,
plus their coordinator using the existing real impact-particle kernel. No
incomplete successful active-scene tick was added. Dynamic GPU extraction and
whole-frame equivalence remain unclaimed; browser movement is still pending.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_quads.c build/native/libtankracer_core.a -lm -o build/native/vehicle_quads_reference.exe
$env:TANKRACER_VEHICLE_QUADS_REFERENCE=(Resolve-Path build/native/vehicle_quads_reference.exe).Path
cargo test -p tankracer-game --test vehicle_quads_reference --locked -- --ignored --nocapture
```

## Shared temporary effects and shield orbit (2026-11-14)

`vehicle_scene::temporary` now owns all four temporary line slots: timestamps,
endpoint/color/phase payload and nine line handles per slot. `vehicle_prefix`
re-exports that same owner, so prefix expiry and after-pose updates cannot use
different copies of a slot. Activation preserves the strict unsigned oldest
selection, already-active timestamp-only refresh, allocator exhaustion aliases,
original endpoint/random ordering and retained phases. Tick commits each line
and row in order; failures retain earlier geometry, color and RNG writes.
Environment startup resets timestamps only, preserving other payload.

`vehicle_scene::shield` owns nine shared strips of four orbits. Initialization
executes exactly **180 global LCG draws**, retaining position payload while
resetting the documented channels/tails. Tick integrates yaw with the old rate
before chasing new rate/pitch targets, uses the original fixed-point tables,
permits intermediate size overshoot and wraps spin to 16 bits. Player/nonplayer
and bounded network-bank selection remain explicit. Its `AfterPose` view reads
the canonical base position, basis and active effect flag. Startup must invoke
initialization at the original point in the shared RNG sequence; a default
zero-valued struct is not initialized world state. Do not replace shared strips
with per-vehicle RNG/state or add an absent once-per-frame update gate.

**VE02: 160 scenarios / 1,280 operations / 1,760,160 words / 8 error prefixes**
match production bootstrap/start/tick/expiry calls. The native oracle counts
actual RNG writes per operation; seed progression and draw counts both match.
Totals: 28,800 bootstrap draws, 12,720 activation draws, 14,224 temporary-tick
draws and 708 shield-tick draws. Includes 256 shield changes, 106 timestamp-only
refreshes, 58 oldest-slot reuses and 33 full-line-pool effects. Failure coverage
includes two activation/retirement failures, four line-update failures and two
explicit expiry failures. Snapshots cover every temporary row, all nine shield
strips, shared seed and all affected line/pool aliases. Only material resolution
is supplied at the external resource boundary. This qualifies these components,
not all side effects of the complete environment bootstrap.

Fresh player, prefix, quad/mark/light and connected-physics regressions pass,
as do workspace/native/Wasm checks, browser build and C preservation. Browser
Wasm is unchanged at 183,235 B. No GPU extraction, moving-browser or whole-frame
equivalence claim; no new per-map precomputation was added.

Next after-pose work: track spray, damage smoke, shared debris and pickup fade,
then the complete coordinator. For spray, inspect the 16-entry surface/type map
and six 23-byte styles. Its stale-position radius offset is immediately
overwritten in all three coordinates before any callback/query; removing those
writes is a candidate source-proven simplification, to qualify by C comparison.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_energy.c build/native/libtankracer_core.a -lm -o build/native/vehicle_energy_reference.exe
$env:TANKRACER_VEHICLE_ENERGY_REFERENCE=(Resolve-Path build/native/vehicle_energy_reference.exe).Path
cargo test -p tankracer-game --test vehicle_energy_reference --locked -- --ignored --nocapture
```

## Track spray and damage smoke (2026-11-14)

`vehicle_scene::{spray,smoke}` implement 439EF0/439550. Persistent particle
payload and timers belong to `vehicle_startup::Player`; live input is read from
the canonical `AfterPose`. Spray preserves last-free-slot selection, pre-expiry
counts, delayed reuse, type-five visibility, all six surface styles, retained
rotation and narrow/wrapping arithmetic. The stale-position radius offset was
removed: all three coordinates are overwritten with the contact point before
any callback/query. The native comparison qualifies that simplification.

Smoke preserves first-free damage reservations, independent turret/body timers,
authored sockets, narrow-before-cap lifetime, retained direction, suppression
and force damping. Severe slip synchronously invokes the existing real impact
generator with the shared pool/seed/cooldown; there is no deferred approximation.

**VP03: 240 scenarios / 1,920 operations / 3,353,520 words / 2 error prefixes**
match C, including immutable styles/socket metadata and per-operation RNG write
counts. Every spray style actually emits (190 emissions); 16 impact-ring changes,
959 smoke changes and 866 RNG draws occur. Cases cover swamp/hardware variants,
full/just-expired particles, threshold sides, seed/coordinate wrapping, shared
line aliases, exhaustion and invalid handles. Like the connected-physics fixture,
the oracle installs source-confirmed impact offsets `[0,0,2000,0,0,-2000]` because
the preserved C bootstrap does not install them; production C remains unchanged.

**VS03: 122 fresh constructions / 83,570 words** now includes all owned spray and
smoke state. Fresh startup, energy, quads, prefix and connected-physics regressions
pass, as do workspace tests, native/Wasm lint, browser build and C preservation.
Browser Wasm stays 183,235 B; no renderer or extra map precomputation changed.
Shared debris/pickup fade, the after-pose coordinator and browser movement remain
pending. These checks qualify components, not a complete moving game.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_particles.c build/native/libtankracer_core.a -lm -o build/native/vehicle_particles_reference.exe
$env:TANKRACER_VEHICLE_PARTICLES_REFERENCE=(Resolve-Path build/native/vehicle_particles_reference.exe).Path
cargo test -p tankracer-game --test vehicle_particles_reference --locked -- --ignored --nocapture
```

## Shared vehicle debris (2026-11-14)

`vehicle_scene::debris` implements the two shared eight-particle banks and the
active/inactive update/clear paths. Per-vehicle markers/cooldown/history belong
to the fresh player; shared bank ownership is separate. Borrowed canonical
owners are retired synchronously when a different vehicle steals a bank.
The kernel reads the current terrain plane, contacts, pose, materials, camera
half-plane and impact accumulator. It preserves wrapped camera arithmetic,
unsigned speed gating, distinct vertical SAR, floor expiry and retained payload.

An invisible nonplayer emission still consumes one audio RNG draw, updates its
cooldown/impact accumulator and sets its marker; it does not generate particles.
A visible emission consumes that draw plus **144** particle draws. The audio
value is exposed as device output without handing RNG ownership to playback;
audio playback wiring remains later work. Clear only resets documented markers,
owner and cooldown, leaving positions/colors/size/previous-plane data intact.

**VD01: 144 scenarios / 1,728 operations / 297,360 words**, alternating nine
vehicles and both banks, match unchanged C with independently counted RNG writes:
255 visible emissions, 142 hidden emissions, 162 owner steals, 57 active clears,
37,117 total draws. Cases include thresholds, wrapping, camera sides, partial
expiry, zero dt, cooldown expiry and mixed material colors. **VS04** adds fresh
debris state: **122 constructions / 83,936 words** match. Native/Wasm lint, 65
game unit tests, browser build and C preservation pass. No extra map metadata
or rendering change; browser movement remains pending.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_debris.c build/native/libtankracer_core.a -lm -o build/native/vehicle_debris_reference.exe
$env:TANKRACER_VEHICLE_DEBRIS_REFERENCE=(Resolve-Path build/native/vehicle_debris_reference.exe).Path
cargo test -p tankracer-game --test vehicle_debris_reference --locked -- --ignored --nocapture
```

## Shared pickup fade (2026-11-14)

`vehicle_scene::pickup` provides typed ray/ring banks and the complete
`frame_vehicle_shared_fade` operation. It preserves target/active gates and the
whole-call return for a bank created this frame, normalized follow motion,
SAR-based growth, old-life color scaling, palette cycling, strict radius gates
and ordered pool retirement with partial state retained on failure. Ray state
colors change here; actual pool projection/activation is separate later work.
The immutable palette and both growth constants are compared with production
bootstrap data. Flare growth remains an explicit externally supplied parameter.

**VF01: 96 scenarios / 768 operations / 2,622,816 words** match production C,
including 43 error prefixes, 265 state changes, 75 bank expirations and 96 calls
that retire allocated records. Tests include same-frame/target gates, zero and
wrapped follow vectors/speed, 16-bit angles, life arithmetic, full palette wrap,
repeated/released/invalid handles, the complete quad bank, exhausted record and
line-bank aliases. The operation consumes no RNG. Native/Wasm lint, 65 game unit
tests, browser build and C preservation pass; no extra map metadata or renderer
change. Activation, projection and full transient bootstrap remain pending M5
work; this does not claim a working pickup or a complete after-pose coordinator.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_pickup_fade.c build/native/libtankracer_core.a -lm -o build/native/vehicle_pickup_fade_reference.exe
$env:TANKRACER_VEHICLE_PICKUP_FADE_REFERENCE=(Resolve-Path build/native/vehicle_pickup_fade_reference.exe).Path
cargo test -p tankracer-game --test vehicle_pickup_fade_reference --locked -- --ignored --nocapture
```

## Complete single-view after-pose chain (2026-11-14)

`vehicle_scene::chain::{active,inactive}` now execute all after-pose kernels in
the production order. Active: spray/marks (special-player gate), pickup fade,
impact animation, smoke/real severe-slip impacts, shared debris, lights,
temporary lines, shield. Inactive: count-gated spray clear, fade, lights,
temporary lines, debris clear, impact animation, count-gated smoke clear, shield.
Errors stop the chain with prior updates intact. No notification replaces a
game operation. The contract is single-player/single-view; network/split-view
scheduling is outside scope. Collision views must already reference the new pose,
as `vehicle::step_physics` refreshes them before its required service call.

`Local` borrows vehicle-owned components and prefix timer/slot. `World` borrows
shared owners/banks and the same effect pool/RNG used for collisions; there is no
second authoritative scene, pose or RNG. The debris accumulator writes directly
through `AfterPose`. Both paths receive live physics data before display scaling.

**VA01: 40 scenarios / 320 complete chains / 2,804,840 fields** match the actual
C active/inactive entry points, with independently counted per-call RNG writes.
There are 210 active successes, 70 inactive successes, 40 error prefixes and
16,052 draws. Every component changes: spray 184, marks 40, pickup 212, impact
rings 63, smoke 236, debris 72, lights 72, temporary rows 192, shield strips 280.
Two owners alternate across eight-operation sequences. Shared-pool contention,
special-player suppression, count-gated cleanup and later-stage failures retain
the correct earlier mutations. Snapshots compare both vehicles, every shared
bank, all line/quads and their bounded aliases. Input is generated typed field
data at the native test boundary, not a saved memory image; production Rust has
no data addresses or C calls. Per-kernel edge cases remain in their focused tests.

Workspace tests, native/Wasm lint, browser build and preservation pass. Browser
Wasm remains 183,235 B; renderer and extra map metadata are unchanged. This
qualifies **the full after-pose chain separately from connected physics**. The
older connected-vehicle fixture's service still only runs impact animation;
wire this real chain into it next, then bind fresh-player scene identity and
scheduling. No complete fresh scene, moving-browser or whole-game claim.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_scene.c build/native/libtankracer_core.a -lm -o build/native/vehicle_scene_reference.exe
$env:TANKRACER_VEHICLE_SCENE_REFERENCE=(Resolve-Path build/native/vehicle_scene_reference.exe).Path
cargo test -p tankracer-game --test vehicle_scene_reference --locked -- --ignored --nocapture
```

## Connected physics with the complete after-pose chain (2026-11-14)

The connected fixture now calls `frame_vehicle_scene_active` in C and
`vehicle_scene::chain::active` in Rust at the real pre-display-rescale service
boundary. It no longer substitutes impact animation for the rest of the chain.
**VC03: 1,254 updates / 17,591,969 words**, across all eight maps and fifteen
vehicles, match for contact, drive, collisions, motion, every after-pose owner,
complete quad/line banks, shared effects and error prefixes. Eight vehicles are
present; two alternate in short sequences. This is still a test-boundary state
comparison, not construction of a fresh independent runtime.

The semantic scene codec visits each field for both reading and comparison.
Shield-bank 0/1 tails alias player-effect words already owned by the main
snapshot and are not duplicated. Native fixture inputs include actual resource
pickup activation, temporary lines, a timed shield, smoke thresholds and
braking/reverse input. A real wide terrain patch is selected by Rust queries and
passed as a test spawn point; C then performs its own unchanged queries. This
exercises plane-based debris without injecting a synthetic post-contact flag.
Viewport visibility is an explicit external input, as no renderer runs here.

Every component actually changes: spray 1,254, marks 422, pickup 88, impact rings
638, smoke 1,254, debris 80, lights 840, temporary rows 88, shield strips 88.
There are 604 peer-write observations, 236 wear observations and 37 real
deformable mesh changes followed by 33 later samples. Native instrumentation
validates each changed seed write is exactly one LCG advance: **14,790 advances**
match Rust. It separately observes **1,166 unchanged seed stores**; the contact
pipeline writes back an unchanged seed when its effect timer is inactive.
Counting those stores as additional draws would incorrectly reject matching
simulation. Native/Wasm production code and map metadata are unchanged; workspace
lint and C preservation pass. Next is fresh scene registration and scheduling.

Rebuild `tests/reference_vehicle.c` with the host/core archives before running
the existing `TANKRACER_VEHICLE_REFERENCE` test. The oracle now receives a verified
ground position from the Rust driver in addition to map/car selection.

## Shared identity and fresh player installation (2026-11-14)

`scene_entity::Handle` distinguishes `Obstacle(ObstacleId)` and `Vehicle(TankId)`
inside one `entity_factory::State`. Scene-object creation, collision callbacks,
named bindings, fragment updates, cleanup and scheduler interfaces now use
that domain. Obstacle-only readers explicitly resolve the obstacle variant;
vehicle indices cannot silently be interpreted as obstacle indices. Existing
class IDs/methods stay stable, with the player class appended to the registry.

`vehicle_loading::Store::create_single_player` uses that common factory to
allocate a stable vehicle slot, construct its real dynamics/model/prefix/effects,
install the class/component binding, prepend the actor drawing membership,
remove hidden membership and activate through the actual tick interface.
The store retains an empty slot if construction fails; no successful empty
vehicle is substituted. Model/resource preparation is an explicit input boundary,
not an opaque original-memory arena. Remaining frame/destroy/player interfaces
return an unsupported-method error until their real owner is installed.

**VS05: 122 fresh registrations / 84,668 words**, all eight maps, three valid
CUP selections, fifteen vehicles, two modes and wobble branches, match native
construction. In addition to all prior dynamic state, the oracle compares class
binding, full allocation/tick flags and filtered tick/visible/hidden memberships.
The entry entity ID still comes from surrounding unported world startup; this
does not claim complete scene identity allocation order or startup RNG replay.

Eight affected native regression suites pass: common Obstacle/Fall resources
(2,937 constructions), fresh World loading (24 scripts / 144 objects), authored
Crush (154), common lifecycle (664 scenarios), Crush updates (178), breakable,
mesh fragments and shared debris. Workspace tests, native/Wasm lint, browser
build and C preservation pass. No renderer or extra per-map data changed;
browser Wasm remains 183,235 B. Native resource-only movement is still pending:
bind prefix/full physics/after-pose to the shared tick method, then run a fresh
resource-initialized probe. Avoid per-frame clones of vehicle dynamics when
projecting the store into the physics API.

## Resource-only native vehicle runtime (2026-11-14)

The runtime adds `vehicle_runtime::SinglePlayer` and the `drive`
example. The fresh resource-created player runs prefix, connected physics and
the complete after-pose chain through the shared factory tick. This is a
terrain/vehicle slice: scene objects, race startup/rules, opponents and browser
integration are not complete. The store's owned player/model/effects are borrowed
through the tick; dynamics and geometry are not cloned per frame. Device feedback
and sound are external services. The two effect-owner words are projected into
the sampling API and published back to their shield-tail owner even on errors;
no shield kernel reads them in this stage. Resolve that alias before mine logic.

Reproduced successfully with no C library, memory snapshot, emulator or server:

```powershell
cargo run -p tankracer-game --example drive --release --locked -- assets/game 0 1 120
```

ENGLAND/car 1: position `[0, 0, 0]` -> `[637, 1172, 37953]`, final velocity
`[9694, -584, 468392]`, 117 grounded updates, final seed `2312014916`.
The repeat measured 1.39 microseconds/update on this host; it is not a
whole-game or browser benchmark. The collider radius correctly uses the body
mesh: the C model loader installs that shape as its collision root.

**VR01: 38 fresh resource scenarios / 1,056 updates / 6,581,504 fields**, across
all eight maps and fifteen cars, compare equal. C constructs its own actor and
retains it before later race/session initialization. Rust constructs directly
from the resource archives, without receiving actor/config state from C.
Each side initializes 16 route-noise draws and 180 shield draws from seed zero.
The comparison covers the initial state, varying 12/16/17/20 ms input, inactive
drive/countdown, acceleration, steering, braking, motion/poses/gun mesh, contact
and prefix fields, local/shared after-pose state, pool payloads and RNG. Two
ENGLAND sequences run 96 updates; other cases run 24. Explicit boundaries are an
empty scenery collision scene, one car, visible viewport and disabled device I/O.
This does not qualify full-world startup allocation/RNG or complete Player rules.

The fresh test caught two real initialization omissions: pickup quad templates
need their first style byte set even while inactive; General's authored CIRCUIT
power multiplier is **60**, overriding the image default 70. `Catalog::general`
now reads it instead of the native example hard-coding the image default.
OC03 qualifies both General settings alongside 19 object presets and six Fall
settings. Shared constructor codecs keep VS05 unchanged (122 / 84,668 words).
Pickup fade and full active/inactive scene regressions pass, as do workspace
tests, native/Wasm lint, browser build and C preservation. The browser is still
static, 183,235 B Wasm; render/collision map metadata are unchanged.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_runtime.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/vehicle_runtime_reference.exe
$env:TANKRACER_VEHICLE_RUNTIME_REFERENCE=(Resolve-Path build/native/vehicle_runtime_reference.exe).Path
cargo test -p tankracer-game --test vehicle_runtime_reference --locked -- --ignored --nocapture
```

The following browser increment shares `vehicle_resources::prepare` with this
example and VR01. The same 38-case C matrix still passes after that resource
path consolidation. The browser now runs `SinglePlayer` directly in Wasm, with
a read-only proxy view and a shared terrain Arc; see the renderer's M3 section
for the actual browser verification and its narrower final-field comparison.
Authored object collision binding is the next M3 slice. Full Player/race rules,
remaining lifecycle methods and complete-world startup RNG are still pending.

## Authored scenery with the native vehicle (2026-11-14)

`vehicle_runtime::Startup` establishes one factory, registry, terrain, scene and
RNG before placing a player. `scenery::load` constructs actual Obstacle/Crush/Fall
records in file order, prepares the canonical sign binding and drains the real
startup queues. `finish` then installs the fresh player into that same identity
domain. Normal frame execution now drains stop/destruction queues after ticks.
Fall's follow-up tick borrows the actual vehicle store to resolve its recorded
last vehicle's reference basis; it no longer assumes every tagged handle is an
obstacle. No per-frame vehicle state or geometry clones were introduced.

The `drive ... --scenery` executable runs the connected path without C, dumps or
an emulator. Default ENGLAND loads **111 actors: 96 Fall, 10 Crush, 5 Obstacle**.
Materials reserve logical identities only for resources present in level/shared
archives; auxiliary models are decoded and retained for their real hit boundary.
The current native test build measured about 1.5-2.7 microseconds/update over
120 updates on this host, excluding resource loading/rendering. This small probe
is not a whole-game benchmark. Its final seed is `3575138284`; scenery construction
consumes the shared stream, so the older empty-scene seed must not be substituted.

This remains a partial-world stage. ENGLAND explicitly defers Bounce (29), Bomber
(5), Closer (1), Flock (3), Noisy (1), Powerup (30), Smoker (1), Trackcam (21),
Train (2) and WayPoint (36). Unknown classes fail before entity mutation.
Other supplied maps also report their actual deferred classes. Bounce/Spin and
other collision subclasses need real bindings; unsupported blast/player methods
still fail. The browser now enables the same supported scenery classes and
reads their current geometry/poses; see [renderer integration](rust-renderer.md#m3-authored-scenery-2026-11-14).
Do not claim complete scenery, full startup RNG/allocation order or M3 completion.

**SR01: all eight valid CUP/map selections, 11 scenarios / 68 updates / 906,126
fields**, including two activated Fall actors and two deformed Crush actors,
compare equal. Both sides construct from resources; no snapshot initializes Rust.
The C fixture feeds the controlled three-class scene to the unchanged World
reader, deliberately stops before lap linking, creates a real Player, and runs
the actual scheduler/queue methods. Its Player tick selects the same direct
physics phase as the Rust M3 controller. Compared state includes the player's
codec, scenery colliders/current vertices, Fall motion, selected Crush hit and
sampling fields, memberships,
tick/stop/destroy lists and RNG at each short checkpoint. Default cases run four
updates; three targeted collision cases run twelve. Shared global visual pools
are covered separately by the earlier connected/full-chain suites.

The joint test exposed a test-codec defect: three collision statistics had been
serialized in the wrong order by the fresh-player codec. Constructor-only zeros
hid it; the existing connected-physics codec was already correct. The shared
fresh-player codec now uses accumulator, size, material order. Production physics
did not need a field swap. VS05 and VR01 still pass with unchanged word counts.
Eleven affected native suites pass, plus `npm run test:rust` and C preservation.
The browser's authored-scenery path now accounts **790,578 B** combined extra-map
metadata, including live rendering and shared collision data. A 120-update Wasm
probe matches native final position/velocity/RNG; three twelve-update browser
collision sequences additionally verify two Fall activations and one Crush
deformation reach the GPU. Read-only rendering does not change simulation
ownership. Required collision subclasses are the next gameplay increment;
transparent draw-call count remains an explicit renderer optimization task.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scenery_runtime.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/scenery_runtime_reference.exe
$env:TANKRACER_SCENERY_RUNTIME_REFERENCE=(Resolve-Path build/native/scenery_runtime_reference.exe).Path
cargo test -p tankracer-game --test scenery_runtime_reference --locked -- --ignored --nocapture
```

## Bounce through the live runtime (2026-11-14)

`bounce` now implements the actual 405170/4051C0 constructors, 404C30 hit and
404E00 tick. The registry adds four distinct Rust bindings; loading constructs
the real parent first, keeps authored/preset width and signedness, and then
applies Bounce configuration. ENGLAND now constructs 140 scenery actors,
including all 29 Bounce objects; 117 actors are in the current render lists.
Spin, Closer and the other explicit deferred classes remain unfinished.

A hit transfers the vehicle's post-response velocity, using forward speed for
the vertical impulse, elasticity/height split for Y/Z and a different X scale.
The relative position in the vehicle basis determines yaw deflection. The
object copies the reference matrix, rotates its actual external basis, enters
airborne mode, activates the scheduler, resets age, derives spin and conditionally
draws one shared random bit above speed 250,000. Basis aliasing is explicit:
equal-valued custom matrices must not be mistaken for shared storage. Collision
views now provide the real vehicle reference/external bases as read-only inputs.

Ticks integrate position before damping spin and rotating. Terrain height is
sampled every four updates, or twenty for special gravity; a miss retains the
cached height and can trigger the fallback surface response. Ground impacts
rebound Y velocity until the original threshold selects rolling. Settling uses
signed X/Z comparisons, not absolute speed; this apparently unusual rule is
preserved. It aligns to the ground normal, clears activity, removes/reinserts
the spatial membership and only then queues stop. Age retirement and missing
ground dispatch the actual deactivate slot. The shared deactivate implementation
is borrowed by both ordinary scene methods and Bounce, with its parent class
dispatch and remove/queue order intact. No mesh or vehicle-state clone is needed
to execute these callbacks.

BIO's inclusive physical rectangle and the low-gravity setting select the
original gravity/sample period. THEME preloads the canonical umbrella identity;
matching objects receive the 750-unit height offset. Auxiliary resources and
remaining classes keep their existing ownership; Bounce does not invent extra
auxiliary update calls that the source tick does not perform.

The new low-gravity integration case exposed another startup omission:
OBPARAMS Moon gains had not replaced initial constants in the suspension
profile. The first update differed by five integer Y units. Reading the actual
player/opponent 3/2 gains fixes it; C physics was unchanged. This is why branch
coverage matters in addition to ordinary driving sequences.

BO01 compares **54 scenarios / 216 operations / 10,489 words** against the
unchanged C kernels. Declared terrain and lifecycle responses cover null/custom/
aliased matrices (including equal-valued separate matrices), speed thresholds,
airborne/rolling/settling, miss/fallback, narrow-counter wrap, signed/overflow
arithmetic and callback error prefixes. These callbacks are test inputs, not a
substitute for integration: **SR02** constructs real parent/resources and runs
the same scene scheduler, spatial lists and collision owners on both sides.
Its **21 scenarios across eight maps / 132 updates / 2,140,565 fields** include
normal and low-gravity settings, two activated Fall actors, three deformed Crush
actors and three actual Bounce hits. The C boundary still deliberately stops
before lap linking and replaces the Player controller with the same direct
physics phase; complete race startup is not qualified by this test.

The scene lifecycle/loading, obstacle, breakable, mesh-break and shatter native
suites still pass after the common adapter change. OC04 verifies the new Moon
settings. `npm run test:rust`, final focused native/GPU/browser checks and source
preservation pass. The browser runs a fourth twelve-update collision case and
observes real Bounce pose uploads, with zero upload on stationary repaint.
See the renderer note for current memory/draw accounting. M3 remains open for
Closer, Spin and remaining required world/startup work.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_bounce.c build/native/libtankracer_core.a -lm -o build/native/bounce_reference.exe
$env:TANKRACER_BOUNCE_REFERENCE=(Resolve-Path build/native/bounce_reference.exe).Path
cargo test -p tankracer-game --test bounce_reference --locked -- --ignored --nocapture
```

## Closer through the live runtime (2026-11-14)

The ENGLAND barrier now uses the actual Closer constructor, hit and tick slots,
with parent construction, stop cancellation and the shared lifecycle owners.
The scene constructs 141 actors and renders 118. Waiting polls the primary
player on the original eight-update cadence and strict X/Z distance boundary.
Closing preserves the integer acceleration, asymmetric rebound thresholds,
65520 roll wrap, queue-before-rotation order and temporary normalization commit.
A hit dispatches the real auxiliary contact method and changes the next tick
to fragment updates. Current matrices and mesh vertices reach the existing
read-only renderer; rendering does not drive the state machine.

`Matrix::angles` imports the original 4,096-entry inverse-cosine table and uses
integer multiplication/division and quantized trigonometry. It preserves the
singular-pitch result and terminal zero. Its declared input contract requires
the normalized pitch component to be within Q12 range; out-of-range source
reads into adjacent memory are not a typed Rust feature. The 8,192-byte table
is global immutable data, not additional per-map precomputation.

**CL01** matches **97 scenarios / 163 operations / 4,575 words** against the
unchanged C Closer tick and matrix conversion. It covers 75 matrix-angle cases,
proximity/cadence boundaries and short closing sequences, speed thresholds,
wrapping arithmetic, null/custom/aliased matrices, queue failures and partial
normalization errors. Lifecycle/fragment callback responses are explicit test
inputs. Constructor and real collision ownership are checked by **SR03**:
**23 scenarios / eight maps / 156 updates / 2,540,971 fields**, including one
closing and one shattered Closer. Its earlier Fall/Crush/Bounce and low-gravity
cases still pass. The controlled world-reader stop before lap linking and the
direct-physics Player replacement remain; this does not qualify full startup.

Focused scene-object/lifecycle, loading, mesh-break and Bounce C suites pass,
as do workspace tests/lints, six browser collision sequences, 144 GPU cases,
native/Wasm final-field comparison and C source preservation. Detailed logs:
`build/tests/rust-closer-*.log`; browser evidence: `build/rust/tests/browser.json`.
Spin, other required collision subclasses and full startup remain M3 work.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_closer.c build/native/libtankracer_core.a -lm -o build/native/closer_reference.exe
$env:TANKRACER_CLOSER_REFERENCE=(Resolve-Path build/native/closer_reference.exe).Path
cargo test -p tankracer-game --test closer_reference --locked -- --ignored --nocapture
```

## Spin through the live runtime (2026-11-14)

`spin` models sliding scenery independently of vehicle physics. The registered
initializer/constructor preserve the Obstacle parent order, preset/override
widths, weight, impact threshold and real activity owner. A hit derives local
impact direction, selects pitch or roll (ties retain the previous axis),
computes yaw from the vehicle-local impact offset, transfers velocity and
activates through the factory. Auxiliary contact dispatch runs afterwards and
clears ordinary motion only after success; feedback remains a device boundary.

Ticks preserve the source order: yaw integration/damping, byte timer advance,
terrain query on the old timer modulo three, matrix normalization on the new
timer modulo ten, ground-normal alignment, edge pivot, horizontal drag, ground/
gravity or fallback response, translation, then the signed stop condition.
Large objects query all four transformed footprint corners until a miss; the
last queried height is the retained edge height, not a minimum/average. Pivot
integration uses the pre-damped angular speed. Matrix aliases are explicit and
normalization errors retain the original partial writes. Ordinary stopping only
queues stop; it does not add a spatial reinsertion absent from the source.

**SP01** compares **88 scenarios / 295 operations / 16,386 words** with the
unchanged C hit/tick. Cases cover impact axes/ties/signs, blocked/already-active
activation, auxiliary dispatch, aliased/custom/null matrices, byte timer wrap,
6,144-radius sampling boundary, exact corner coordinates/query order, terrain
miss/fallback, slope alignment, pivots, signed/overflow arithmetic, damping,
stopping and callback/normalization error prefixes. Terrain/lifecycle/device
responses are declared inputs; parsing is not qualified by this kernel test.

**SR04** uses the actual SWAMP trolley and EAST ticket/trolley resources through
real constructors, terrain, scheduler and vehicle collisions. Its **27 scenarios
/ eight maps / 204 updates / 3,316,937 fields** match C, including four targeted
Spin sequences and five struck Spin actors. Preset construction and all earlier
Fall/Crush/Bounce/Closer cases still pass. Complete Player/race startup remains
outside the declared world-reader/direct-physics boundary. Arbitrary constructor
failure prefixes and every parameter override are not separately qualified.

The browser runs two additional twelve-update Spin collisions using the real
SWAMP archive and terrain, and observes actual instance uploads. It infers map/
cup settings from supplied archives rather than labelling every session ENGLAND;
production startup still selects ENGLAND. The SWAMP download is test-build only.
Related native suites, workspace checks, eight browser collision sequences,
144 GPU cases and C preservation pass. Logs: `build/tests/rust-spin-*.log`.
Mover (a Spin subclass) and remaining full startup/lifecycle work are next.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_spin.c build/native/libtankracer_core.a -lm -o build/native/spin_reference.exe
$env:TANKRACER_SPIN_REFERENCE=(Resolve-Path build/native/spin_reference.exe).Path
cargo test -p tankracer-game --test spin_reference --locked -- --ignored --nocapture
```

## Mover construction/movement increment (2026-11-14)

Mover inherits the actual Spin initializer/constructor, current velocity,
timer, matrix and collision method table. It has its own flags, travel, speed,
endpoint distance and player activation range. Source boolean keys are unusual:
an absent key clears its bit, including bits set by the initializer or preset.
Speed conversion wraps the left shift before signed division by 1,000. The
constructor preserves exact-key `:lights` render-list movement, sets velocity
from the current basis and cancels the real stop notification before activity.
Disabled positional audio remains outside this increment.

Before activation, old timer modulo five gates the two-player proximity check;
its metric is max(abs X, abs Z) plus the rounded-up half of the smaller. After
activation, the primary camera's original divided-by-eight coordinates gate
movement using strict per-axis bounds. The authoritative camera is supplied
before scheduling; the renderer does not advance this state machine. Active
motion integrates X/Z, optionally queries/aligns terrain every third old timer,
then advances travel. Endpoint comparison is strict. Oscillation removes one
distance and either negates speed or rotates by **32760**, not a rounded half
turn; it then rebuilds velocity. Nonoscillation clears activity before queueing
stop. Flags can instead dispatch the real parent Spin tick or debris update.

**MV01**: **51 scenarios / 177 operations / 6,635 words** match unchanged C
construction/tick kernels. Declared callback responses cover key aliases,
missing booleans, numeric wrap/EOF behavior, lights-list/cancel failure prefixes,
player absence/proximity, exact camera bounds, timer wrap, terrain miss/alignment,
null/custom/aliased matrices, stop/reverse/turn endpoints and signed arithmetic.
Parent/list callbacks are inputs here, not substitutes for owner integration.

**SR05**: **30 scenarios / eight maps / 240 updates / 4,175,411 fields** match
the C resource/factory/scene owners. BIO adds near/far-camera movement sequences
and one nonexploding collision-to-Spin handoff. That handoff is a declared input
variation: all nine `:explode true` tokens in BIO B/S/G are changed to equal-width
`:explode faux` in a transient archive copy, preserving every directory span
and shape/terrain byte. Original assets remain unchanged. Other scenarios use
unmodified resources. The pre-lap world-reader and direct-physics Player test
boundaries remain; full Player/race startup is still unqualified.

At this checkpoint, original BIO explosive hits still lacked their entity
binding. The following increment closes that dependency; complete M3 startup
and lifecycle remain unfinished.

Focused Mover/Spin/Closer/Bounce, scene/loading, mesh-break and shatter native
suites pass, along with workspace checks and C preservation. A prior mesh-break
test used method ID 90 as an unknown-method sentinel; 90 is now the real Mover
tick. The test now uses `u16::MAX`, preserving the intended unsupported-method
branch. Browser evidence adds one real BIO twelve-update activation/movement
probe (eight collision probes remain), 144 GPU cases, reloads, native/Wasm final
fields and zero stationary uploads. Logs: `build/tests/rust-mover-*.log`.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_mover.c build/native/libtankracer_core.a -lm -o build/native/mover_reference.exe
$env:TANKRACER_MOVER_REFERENCE=(Resolve-Path build/native/mover_reference.exe).Path
cargo test -p tankracer-game --test mover_reference --locked -- --ignored --nocapture
```

## Original Mover explosion and blast ownership (2026-11-14)

The unmodified BIO robot now spawns an actual kind-zero explosion through the
shared class table, entity factory, activation lists and scheduler. Common
allocation is extracted without adding implicit memberships or RNG calls.
The original first random draw precedes activation; initialization consumes the
second. Fifteen slots retain unsigned birth frames, strict earliest-frame
selection and first-slot ties. Eviction removes the old drawing membership and
queues actual deactivation. Retirement waits for the main sheet and all smoke,
clears the slot, removes drawing, queues deletion and decrements the count.
The class explicitly overrides RELEASE with ID release: the reuse branch caught
an initially missing override that ordinary spawn-only checks did not expose.

The explosion kernel preserves main-sheet UV progression, rotation, packed
damping and all ten puffs' generation, movement, growth, rotation and fade.
Mover's hit invokes its actual debris contact, removes the collider and ordinary
membership and schedules fragments. The single-vehicle CUP adapter obtains the
target from authoritative vehicle state and applies the real blast impulse;
velocity, angular rates and collision marker publish through the collision view.
The blast kernel preserves thresholds, health-write/error prefixes and all
twelve RNG draws, including discarded/sign draws. Resource/device feedback
still delegates to the host. No private effect entity IDs or detached RNG.

**BL01:** 70 scenarios / 274 operations / 4,384 words match unchanged C. Branches
cover force thresholds, direction/overflow, health clamp and division errors,
angular/axis/marker gates and short repeated impulses. The kernel covers the
source modes; the runtime adapter deliberately binds single-player CUP modes
0/1 only, not deathmatch or multiple vehicle owners.

**SR06:** 37 scenarios / eight maps / 348 updates / 7,462,914 fields match C.
Snapshots now include current 3D debris and all live explosion/smoke fields,
slots, entity flags, memberships, queues and shared RNG. Added original BIO
contact plus controlled final-sheet retirement, smoke-delayed retirement,
tenth-puff generation, ID reuse and sixteen-spawn saturation with tied and
different birth frames. Controlled branch inputs are applied equally to both
owners; they are not long natural-race claims. The C snapshot scans entity
capacity, since active count can miss a high live ID after deletion. The level
arena ends before dynamic effects, as in gameplay. The existing pre-lap loader
and direct-physics Player boundaries remain: full race startup is unqualified.

Focused native vehicle, motion/pipeline, obstacle, scenery/loading and fragment
suites pass (logs `build/tests/rust-explosion-*.log`). Workspace tests/lints and
source preservation pass. Browser evidence adds a tenth twelve-update case:
original BIO contact produces one active explosion and active debris, with no
GL errors or stationary uploads. Full effect drawing is still pending; this
proves gameplay ownership and state, not appearance.

Remaining explosion kinds, attached/ring/band variants, weapons and render
resource bindings belong to later effect work. Unsupported variants return
explicit errors rather than successful no-ops. M3 still needs full required
collision/startup/lifecycle bindings and world allocation/RNG qualification.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_blast.c build/native/libtankracer_core.a -lm -o build/native/blast_reference.exe
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_scenery_runtime.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/scenery_runtime_reference.exe
$env:TANKRACER_BLAST_REFERENCE=(Resolve-Path build/native/blast_reference.exe).Path
$env:TANKRACER_SCENERY_RUNTIME_REFERENCE=(Resolve-Path build/native/scenery_runtime_reference.exe).Path
cargo test -p tankracer-game --test blast_reference --test scenery_runtime_reference --locked -- --ignored --nocapture
```

## CUP Player input and real keyboard path (2026-11-14)

`player_input` owns digital ramp/release, forward/reverse control selection,
analog steering latch and twin-track rules. Host action bindings and normalized
axis queries are explicit devices, including errors at the actual query point.
The defaults (800 throttle/steer push, 70 brake push, 720 throttle/brake return,
700 steer return) come from the immutable C defaults. Return words are signed
16-bit; products and negations wrap before signed comparisons. Input state is
per Player, the analog latch is shared, and countdown throttle retains its
existing prefix owner rather than a second persistent copy.

At zero speed, brake can select negative throttle; at negative speed, the keys
exchange acceleration/braking roles. Left wins simultaneous steering presses.
Digital input overrides analog latching. Axis zero is queried even without a
per-player analog gate, so device errors preserve earlier throttle/brake writes.
Countdown stage four is an early return; other countdown stages clamp only
preheat throttle, leaving old brake/steer values intact. Start-stage/press state
feeds the real fast-start branch in parent physics.

**PI01:** 226 scenarios / 1,341 operations / 20,890 words match unchanged C,
including the device query trace. All digital combinations, signed speeds,
countdown stages and held flags, analog endpoints, twin tracks, signed rate and
product overflow, player gates, releases and axis-failure prefixes are checked.
The oracle's initial trace accidentally counted the twin-track right field as
an axis query because they alias; it now identifies the actual normalization
read sequence. Production logic was not changed for that fixture correction.

The runtime's `step_keys` applies input at the Player's actual scheduler position
before prefix/parent physics. Raw `step` remains available for physics tests.
The browser now passes booleans every fixed update, replacing direct 65536/32768
assignments. Its direct-value Wasm export is compiled only with `gpu-tests`.
The native example's `--keys` uses the same bound owner. The proxy still starts
in driving mode: full countdown timing/menus are not introduced by this slice.

The expanded fresh-runtime oracle compares both paths on all eight maps and
fifteen cars: **1,968 updates / 12,302,404 fields**, including input state,
preheat, launch, real shared effects/RNG, contact and vehicle updates. New input
sequences are 24 updates each; earlier direct-physics sequences remain. Browser
input/native final fields match over 24 updates, alongside the existing direct
120-update test, GPU tests, ten scenery probes and zero idle uploads. Workspace
checks, related constructor/prefix/scenery oracles and C preservation pass.

**At this checkpoint incomplete:** auxiliary brake/parameter restoration, collision/progress
Player prefix, turret/fire, route/lap rules, environment/palette and lifecycle.
The bound key API explicitly rejects auxiliary braking until its actual owner
is implemented; the browser does not bind it yet. The input kernel is CUP's
driving/countdown region, not the deathmatch-restoration branch or a complete
Player tick. No full race or general per-frame browser-parity claim.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_player_input.c build/native/libtankracer_core.a -lm -o build/native/player_input_reference.exe
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_vehicle_runtime.c build/native/libtankracer_host.a build/native/libtankracer_core.a -lm -o build/native/vehicle_runtime_reference.exe
$env:TANKRACER_PLAYER_INPUT_REFERENCE=(Resolve-Path build/native/player_input_reference.exe).Path
$env:TANKRACER_VEHICLE_RUNTIME_REFERENCE=(Resolve-Path build/native/vehicle_runtime_reference.exe).Path
cargo test -p tankracer-game --test player_input_reference --test vehicle_runtime_reference --locked -- --ignored --nocapture
```

## Player handbrake and parameter restoration (2026-11-14)

`player_brake` now operates on the Player's actual drive input, model max-turn
and yaw-lock fields. Initial alternate lateral/rotation coefficients come from
the already-qualified model, not zero defaults. Only speed magnitude strictly
above 250,000 enters the override: signed16 lateral grip falls by `2*dt`, rotation
damping changes and the low16 turn limit is saved before installing 310. Holding
the key also sets brake 40,000 and throttle zero below that speed threshold.

Release restores the previous primary surface's dry-slide multiplier and model
rotation damping, with source signed16 wrap/comparison rules, then turn limit by
`4*dt`. Restoration must finish all three before clearing the alternate flag.
An active temporary effect resets these parameters immediately and deliberately
leaves the reverse-braking flag unchanged. That flag now feeds the existing
actual vehicle-light chain. Surface lookup is lazy: ordinary/unmodified ticks
and fast-brake entry do not read a previous material they do not need.

**PB01:** 97 scenarios / 1,164 operations / 12,804 words match unchanged C,
covering both signs/exact speed thresholds, MIN absolute overflow, signed widths,
rate/product wrap, saved-turn truncation, hold/release, temporary suppression,
completion and isolated mode4/5 restoration/rearm. Runtime support remains CUP
0/1. Suppression is kernel-qualified; full pickup activation is later work.

Fresh runtime comparison now includes high-speed held/released keys: eight maps,
fifteen cars, **2,880 updates / 18,041,268 fields**. These additional scenarios
use a declared initial velocity, not captured state. Common Player snapshots
also include all override fields/max-turn: 122 constructions / 85,400 words and
scenery 348 updates / 7,465,224 fields pass. A preliminary fixture set a temporary
timer without creating its real effect slot; the C after-pose owner correctly
rejected it. That invalid fixture was removed rather than inventing a successful
pickup owner. The standalone suppression branch remains tested equally in C/Rust.

Browser A press/release runs through real DOM events and fixed ticks. A separate
24-update high-speed sequence matches native Rust physics/RNG and final control
fields and asserts actual override activation. GPU/GL, ten scenery probes,
reload/idle upload, workspace lint/tests and C preservation pass. Controlled
load/inspection exports remain test-only; production Wasm is 693,690 B. No new
map preprocessing. Logs: `build/tests/rust-player-brake-*.log`.

Next Player binding: active-race collision marker/impact decay and vector-change
contributions before parent physics. Full Player/race startup remains unfinished.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_player_brake.c build/native/libtankracer_core.a -lm -o build/native/player_brake_reference.exe
$env:TANKRACER_PLAYER_BRAKE_REFERENCE=(Resolve-Path build/native/player_brake_reference.exe).Path
cargo test -p tankracer-game --test player_brake_reference --locked -- --ignored --nocapture
```

## Player collision-indication prefix (2026-11-14)

`player_collision` now consumes the actual preceding ground/tank deltas and
obstacle impulse, before auxiliary brake and parent Tank physics. In an active
race the signed marker decays by dt, then each magnitude strictly above 30,000
adds `(change-30000)>>11`; the final upper cap is 1,250. The obstacle value is
already a signed response magnitude: no abs or vehicle-speed substitution.
The existing unsigned-magnitude approximation is reused from `geometry`, with
dominant-axis ties and MIN/overflow behavior qualified against the Player helper.
Impact indication decays separately and preserves its source signed comparison.
The existing canonical collision fields own every write, not another flag bank.

The runtime has an explicit `race_active` gate distinct from driving/countdown.
The current proxy initializes it true; the future race controller must own its
transitions. The raw physics API remains outside the Player prefix. No complete
race-state or HUD-rendering claim follows from the indicator update alone.

**PC01:** 114 cases / 912 operations / 10,944 words match unchanged C. Cases
cover gate bypass, exact thresholds/quantization, all dominant-axis ties,
unsigned abs extremes, marker cap and signed decay/wrap. The fresh-runtime
comparison still passes **2,880 updates / 18,041,268 fields** on eight maps and
fifteen cars. High-speed scenarios now declare initial collision deltas/marker
and exercise active-gate changes; an assertion requires the prefix to consume
those initial values. Source/Rust run the actual input -> indication -> brake ->
parent sequence, while route/lap/environment and full Player lifecycle remain
outside this oracle boundary.

Workspace checks, actual browser keyboard/handbrake, native/Wasm final fields,
GPU/GL, ten short scenery probes and idle-upload checks pass. Production Wasm:
**694,304 B**; no added map precomputation. Logs:
`build/tests/rust-player-collision-*.log`. Next required startup dependency is
authored WayPoint construction/linkage and its ordered grid before route/lap
progress can run on actual resources; an isolated progress helper is insufficient.

```powershell
& C:/Develop/mingw/bin/gcc.exe -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_player_collision.c build/native/libtankracer_core.a -lm -o build/native/player_collision_reference.exe
$env:TANKRACER_PLAYER_COLLISION_REFERENCE=(Resolve-Path build/native/player_collision_reference.exe).Path
cargo test -p tankracer-game --test player_collision_reference --locked -- --ignored --nocapture
```
