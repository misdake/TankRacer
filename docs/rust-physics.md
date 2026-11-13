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
