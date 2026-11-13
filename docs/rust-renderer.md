# Rust renderer: M1 foundation and M2 static world

The Rust browser shell uses `assets` for immutable source data, `scene` for
native-testable game-specific preparation/camera, `render` for typed geometry,
materials and WebGL, and `web` for browser ownership. Simulation has not migrated.

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
