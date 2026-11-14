# Rust renderer

Current live scenery integration and its limits are recorded in
[M3 authored scenery](#m3-authored-scenery-2026-11-14). Earlier sections describe
the static-world and proxy foundations and their original measurements.

The Rust browser shell uses `assets` for immutable source data, `scene` for
native-testable game-specific preparation/camera, `render` for typed geometry,
materials and WebGL, and `web` for browser ownership. The `game` crate now runs
the terrain/vehicle update slice; the full race remains in the C reference.

The renderer uploads indexed, world-space vertices once. Vertex shaders perform
the matrix transform and hardware performs projection clipping. The frame sends
matrices/material parameters, not a freshly projected triangle stream. ENGLAND's
612 source meshes become 5 indexed draws/8,762 visible triangles. Vertex/texture
data is not uploaded per frame. Transparency reorders 20,508 index bytes when
the camera changes, and uploads nothing when the camera is stationary.

`Pipeline` specifies depth comparison/write, RGB/alpha blend factors/equations,
culling and color write masks. Each frame resets framebuffer/viewport/clear
state and invalidates the state cache. Each material supplies its own texture
usage and alpha cutoff, so those cannot leak from a previous draw.

`Frame` exclusively borrows `Renderer`: resource upload/allocation cannot
interleave draws and invalidate cached bindings. GPU handles have ownership and
release on drop, including partially failed resource creation. All Rust crates
forbid unsafe code. Mesh/texture bytes are copied during resource upload only.

## Static world and materials

Coordinates convert once: BSM `(X,Y,Z)/128` becomes right-handed `(X,Y,-Z)/128`.
Simulation positions are eight times the BSM/published camera units; they will
remain integer and unreflected. Original focal length 512 at height 480 defines
the inspection camera's FOV. Its expanded far plane is for map inspection.

Source normals establish triangle winding after the Z reflection. Hidden meshes,
disabled triangles and exact degenerate faces are accounted separately. Original
billboard centers/widths produce a per-vertex horizontal offset; the shader turns
them using the camera right axis. No CPU projection or per-frame vertex expansion.

One 1024x512 atlas holds the 56 ENGLAND world material identities plus white for
untextured faces. It uses the original clamp/linear, base-level sampling policy,
with extruded gutters and no added UV half-texel shift. Repeat/mipmapped resources
must use separate policies; they are not silently squeezed into this atlas.

Atlas texels are explicitly premultiplied before filtering. Opaque/masked draws
recover straight color; blended draws output premultiplied color with ONE and
ONE_MINUS_SRC_ALPHA. Both paths preserve the same coverage equation and avoid
purple/dark fringes. Source flags distinguish opaque, color-key and alpha blend;
foliage alpha textures are not arbitrarily changed to alpha-test materials.

Opaque/masked faces have depth tests/writes and hardware culling, with separate
two-sided batches. Transparent faces have depth tests, no depth writes, and
global back-to-front triangle order. Per-face shader culling allows one ordered
alpha draw without turning one-sided surfaces into double-sided surfaces.
Centroid sorting is the current conventional approximation for intersecting
transparent triangles; further scene/effect validation remains in M4/M8.

Source mesh/face offsets and retained index lists preserve ownership for future
detachable features. In Wasm their storage plus transparency metadata/sort work
plus container headers, atlas-region/name metadata is 376,808 bytes, counted
toward the shared per-map acceleration budget. Physics
acceleration must account for this existing usage, not claim another 2 MB budget.
GPU resource storage is reported separately: 743,600 vertex bytes, 105,144 index
bytes and 2,097,152 atlas bytes. Source/load scratch allocations are transient;
CPU vertex/atlas copies are released after upload.

This milestone displays static BSM content. Placed actors, sky/weather, animated
material layers, shadows, vehicles and gameplay remain subsequent milestones.

## Checks

`cargo test --workspace` validates all 15 supplied FASTFILE archives, truncated
directories/invalid resource ranges, and the strict map-acceleration budget.

`npm run test:rust` additionally builds a GPU-test feature and launches Chromium.
An RGBA8/depth24 offscreen target verifies all four output channels directly:

- All eight depth functions with nearer/equal/farther fragments.
- Depth write on/off.
- 81 ordered blend transitions among opaque, straight/premultiplied alpha,
  additive, alpha-additive, multiplicative, subtract/reverse-subtract and min/max.
- Alpha discard/depth behavior and restoration to untextured opaque drawing.
- Culling and color write masks, including their restoration.
- Next-frame clearing after a draw leaves depth/color writes disabled.

M2 adds premultiplied/straight input-output equivalence at filtered silhouettes,
GPU atlas borders, mixed transparent face culling, billboard basis and index-only
updates. 133 pixel cases use independent numerical expected results. The browser
test also checks the actual ENGLAND scene, keyboard/pointer camera input, zero
stationary uploads, per-map memory accounting, three aspect-ratio layouts and
absence of runtime errors/API requests. Results/screenshots are ignored outputs
under `build/rust/tests`. It uses software WebGL for reproducible CI; physical
GPU/browser performance is a later, separate measurement.

Production builds omit GPU probes. The C source, native program and previous
browser site are unchanged and remain available to differential tests.

## M3 proxy foundation, before authored scenery (2026-11-14)

The normal Rust page now loads DATA, BRONZE and INGAME resources and drives a
fresh vehicle in Wasm. A shared `vehicle_resources::prepare` path serves both
the native example and browser; raw archive mounts are transient. Rendering and
simulation share one decoded `Arc<TerrainData>`; no collision graph is cloned.
Arrow keys supply throttle/brake/steering at fixed 16 ms ticks. C toggles the
temporary follow/free camera; `/?inspect` preserves static inspection. These
are M3 controls/camera, not the final Player controller or original chase camera.
No race rules, placed scenery collision actors or gameplay menus are claimed.

The render view receives integer pose/bounds through a single Q3/Q12 coordinate
boundary. Source local axes form matrix columns, with both local and world Z
reflected so right-handed mesh winding remains intact. A native translated
quarter-turn test checks this independently. The unit cube uploads once; its
transform changes without streaming vertex data. Opaque world -> proxy ->
transparent world ordering retains depth testing/writes and premultiplied blend
semantics. The moving view uses **6 draws / 8,774 triangles**; 133 GPU cases and
the existing static keyboard/pointer/layout/upload checks still pass.

`npm run test:rust` now also launches the normal page, verifies actual keyboard
driving and compares a fresh 120-update Wasm run with the independently launched
native Rust example. Final position `[637,1172,37953]`, velocity
`[9694,-584,468392]`, RNG `2312014916` and frame 120 agree. This browser check
compares those final fields, not every field of every frame; VR01 separately
qualifies the fresh native path against C. A screenshot confirms the visible
proxy and scene. A local headless run took about 0.8 ms for the 120 calls,
including JS/Wasm calls and view/camera updates, excluding drawing/loading.
This is not a whole-frame or hardware-GPU benchmark.

ENGLAND now enforces the **combined** per-map budget at load: **638,906 B** in
Wasm, comprising 377,176 B render metadata (including browser/GPU-handle metadata)
and 261,730 B collision data plus its Arc header. Source/GPU storage and dynamic
vehicle/effect state remain separate. Other maps still need their combined live
budget measured when enabled. Production Wasm is **485,537 B**; test-only GPU
probes and vehicle inspection exports are omitted. M3 remains open for authored
scenery collision binding before the M4 vehicle renderer.

## M3 authored scenery (2026-11-14)

The browser now uses `Startup -> scenery::load -> finish`, matching the qualified
native scene path. ENGLAND constructs 111 Obstacle/Crush/Fall actors. The current
ordered render memberships contain 87 ordinary and seven special actors; the
other 17 are outside the current lists. Respect these lists, including lap-gated
activation, rather than drawing every constructed mesh. Complete lap/race scheduling
and the explicitly deferred classes remain unfinished.

`scene::model` prepares local-space geometry and a separate scenery atlas, using
the same color, winding and material classification as the world. Atlas inputs
include preloaded auxiliary models, with level resources taking precedence over
shared resources. The mutable mesh store retains source-material bindings and
a change version across private copies. Rendering reads the current mesh handle,
geometry and collider basis, not the immutable original model. World and local
meshes remain GPU-resident; the shader applies object transforms and billboard
offsets before projection. CPU vertex/atlas upload scratch is released.

Compatible opaque/masked actors share a prototype and matrix-instance stream.
Transforms upload only when membership or a pose changes. Crush edits update the
affected prototype's vertices; topology/pass changes rebuild that prototype.
Retired private meshes release their GPU resources without renumbering handles.
Normal draws explicitly disable matrix arrays and restore uniform transforms;
instanced draws bind the required stream for the actual VAO. Uploads still occur
outside the exclusively borrowed `Frame`.

World and actor transparency use one back-to-front centroid order. Index banks
remain attached to their resident prototypes; consecutive faces from the same
instance become one range draw. Shared prototypes can repeat indices for multiple
instances. Each run supplies its own atlas/model, premultiplied blend, depth test,
disabled depth writes and per-face culling. Identical-pose session reloads reset
the order cache because their new GPU buffers still need sorted banks.

The 120-update default probe submits **9,895 triangles / 385 draws**: four world
opaque/masked passes, 34 scenery opaque/masked passes, the proxy and 346 globally
ordered transparent runs. This is a measured remaining performance cost, not the
final draw-call target. Future transparent batching must preserve global order;
grouping a whole actor before/after the world is not an acceptable shortcut.
No frustum/distance culling is applied to these actors yet. Pipeline and upload
counts remain observable through test/inspection exports, not an on-screen HUD.

Combined resident extra-map accounting is **790,578 B in Wasm**: 261,730 B shared
terrain/collision data plus 528,848 B render/bookkeeping. This includes scenery
atlas metadata/names, matrix/sort/index capacities, GPU handle metadata and
conservatively complete mesh-handle slots/material Arc allocations. Original
geometry, dynamic simulation state and actual GPU storage are separate. The
2,000,000 B guard is checked after scene changes; all tested views stay below
1,000,000 B. The unused static-only order capacity is conservatively still counted.

Verification extends `npm run test:rust` to **144 GPU pixel cases**, including
same-VAO ordinary/instanced transitions, another VAO/stream, index-bank growth and
range offsets, current vertex uploads, model transforms and a world/object/world
alpha composition checked against independent source-over equations. Existing
eight depth functions and 81 blend transitions remain covered. Native tests
prepare 52 actual ENGLAND models / 886 prototype triangles, check deformation
reaches vertices without changing source UV/color, and check disabled faces.

Browser tests compare the final player position, velocity, RNG and frame after
120 updates with native `drive ... --scenery`: RNG is now **3575138284**. This
remains a final-field check, not full per-frame state parity. Two identical-pose
reloads and three controlled twelve-update hits cover two Fall activations and
one Crush deformation. Fall uploads changed transforms; Crush uploads changed
vertices. Repainting without simulation uploads zero indices, vertices and
instance data. Actual WebGL error checks and screenshots cover those paths.
Full fragment-effect appearance, original vehicle/camera rendering and complete
race startup are still outside this increment.
Production Wasm is **629,704 B**; GPU probes and controlled collision starts are
test-only. The working C reference still passes source-preservation checks.

## M3 Bounce increment (2026-11-14)

The same live renderer now displays Bounce actors from their actual poses;
physics/lifecycle belong to the game crate. ENGLAND constructs 140 actors and
currently draws 117 (110 ordinary, seven special), including Bounce actors.
The default probe has **395 draws / 10,025 triangles**: 36 scenery
opaque/masked draws and 354 globally ordered transparent runs. Those runs remain
an explicit optimization task. The fourth controlled collision case activates
a real Bounce actor and uploads changed instances; repeated paint has zero
index/vertex/instance upload. All 144 GPU pixel cases, actual WebGL error checks,
reload checks and native/Wasm final-field comparison still pass.

Combined extra-map bookkeeping is **796,288 B**, including 261,730 B shared
terrain/collision and 534,558 B rendering/bookkeeping. This is 5,710 B above the
previous scenery increment and still below 1 MB in every tested view. Production
Wasm is **643,787 B**. Original vehicle rendering, complete lighting/effects and
the deferred actor classes remain later work; this is not full image parity.

## M3 Closer increment (2026-11-14)

The existing current-mesh/instance path now includes the real ENGLAND Closer
barrier: 141 constructed actors, 118 in render lists. The default 120-update
probe submits **396 draws / 10,033 triangles**, including 37 scenery
opaque/masked draws and 354 globally sorted transparent runs. Transparent
batching/culling remains pending; adding the barrier does not change pipeline
state or ordering rules.

Two additional twelve-update browser cases cover Closer proximity/rotation
and collision/shattering. The first uploads transforms (512 B), the second
changed vertices (10,560 B) and transforms (64 B). State assertions use actual
scene components, not screenshots. Both stationary repaints upload zero bytes.
The camera in the proximity test looks away from the barrier; that screenshot
alone does not establish visible barrier rotation. Actual GL error checks,
all 144 GPU pixel cases, reloads and native/Wasm final-field checks still pass.

Combined live extra-map accounting is **797,177 B** = 535,447 B rendering/
bookkeeping + 261,730 B shared collision data. The tested shattering view peaks
at **811,681 B**, still below 1 MB; source resources/dynamic game data/GPU bytes
remain separately accounted. Production Wasm is **658,917 B**; controlled
collision starts and GPU probes remain test-only. Full lighting, vehicle/camera
rendering and complete image parity are not established by this increment.

## M3 Spin increment (2026-11-14)

SWAMP trolley instances now render the authoritative Spin poses after real
collisions. Two twelve-update browser probes exercise Z/X incoming vehicle
velocities; each uploads **1,280 B** of changed matrices and no vertices.
The stationary repaints upload zero indices/vertices/instances, with actual
WebGL error checks. Existing eight depth functions, 81 blend transitions and
144 pixel cases pass. These tests establish state/pose delivery and pipeline
behavior, not complete SWAMP lighting or image parity.

The SWAMP views draw **83 actors / 1,392 actor triangles**; the final probes have
**657 / 655 draws**, **12,118 total triangles**, 14 scenery opaque/masked draws
and **639 / 637 transparent runs**. This reinforces the measured need to reduce
transparent draws while preserving global order. Actor frustum/distance culling
and complete lighting remain pending.

Combined SWAMP extra-map data is **926,310 B** = 660,758 B rendering/bookkeeping
+ 265,552 B shared collision data. It stays under the preferred 1 MB target;
the 2 MB runtime guard still applies. ENGLAND remains 797,177 B (811,681 B in
the tested shattering case), 396 default draws and 10,033 triangles. Other maps
still need combined live accounting. Original resources/dynamic simulation/GPU
storage remain separately accounted; Spin adds no per-map query cache.
Production Wasm is **670,727 B**; controlled starts/state probes are absent
from its generated JavaScript exports.

## M3 Mover motion and redundant-cache removal (2026-11-14)

The ninth controlled browser probe loads real BIO terrain/resources and
activates a robot without colliding with its unfinished explosive hit path.
Twelve updates upload **2,112 B** of changed instances, no vertices; stationary
repaint uploads zero bytes. BIO renders 72 actors / 1,446 actor triangles;
the final probe has **503 draws / 11,522 triangles**, 20 scenery opaque/masked
draws and 479 globally ordered transparent runs. Screenshots show only the
controlled proxy/movement view, not full lighting or image parity.

The first BIO probe exposed **1,003,668 B** combined extra-map data, 3,668 B
above the preferred target. The renderer retained a static-only transparency
sort cache after switching to the joint world/actor sorter. Installing a live
session now releases the unused cache and subtracts its actual capacities from
both owning counters; identical-pose reloads release zero a second time. App
returns to static inspection by loading a fresh WorldView. A resource test
compares exact static/joint world index order and checks idempotent release.
No blend/depth/material or draw-order rule changed.

Resident live totals after release: **ENGLAND 762,997 B** (34,180 B saved;
777,501 B tested shattering peak), **SWAMP 875,170 B** (51,140 B saved),
**BIO 946,548 B** (57,120 B saved = 664,438 render + 282,110 collision).
All three tested maps stay below 1 MB. Actual GPU/resource/dynamic game storage
remains separate. Existing draw/triangle counts are unchanged. All 144 GPU
pixel cases, eight depth functions, 81 blend transitions, reloads and GL checks
pass. Production Wasm: **681,225 B**, without controlled-start/state probes.

## M3 original BIO explosive contact (2026-11-14)

The tenth controlled browser case exercises the unmodified robot's explosive
hit. Actual simulation state contains one explosion and active debris after
twelve updates. The renderer still draws ordinary current actor meshes/poses;
explosion/smoke sprites and complete fragment geometry are not yet bound. This
case does not qualify their appearance or effect blending. It checks GL state,
real simulation progress and zero stationary uploads; existing 144 GPU cases,
eight depth functions, 81 blend transitions and native/Wasm final fields pass.

The final contact view reports 556 draws / 11,522 triangles, 20 scenery
opaque/masked draws and 532 global transparent runs. Combined BIO extra-map
data remains **946,548 B**; no new per-map query cache. Explosion entities are
dynamic simulation memory, not preprocessing. ENGLAND/SWAMP budgets are unchanged.
Production Wasm is **690,915 B**, with controlled test probes absent. Source and
test boundaries are detailed in `rust-physics.md`; original vehicle rendering,
full lighting, fragment/effect rendering and race integration remain pending.
