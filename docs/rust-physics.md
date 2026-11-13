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

Next integration order: typed deformable geometry → primary sampling and its
surface-noise/effect state → material/fallback/layout/lazy auxiliary/suspension
pipeline → vehicle resource config and drive/pose/collision → browser proxy.
Keep the exact dependency order, including previous-gap and stale-cache behavior.
