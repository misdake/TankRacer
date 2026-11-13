# M1: renderer foundation

The Rust browser shell uses three small crates: `assets` for immutable source
data, `render` for typed geometry/materials and the WebGL backend, and `web` for
browser ownership and the temporary demo scene. Simulation has not migrated yet.

The renderer uploads indexed, world-space vertices once. Vertex shaders perform
the matrix transform and hardware performs projection clipping. The frame sends
matrices/material parameters, not a freshly projected triangle stream. The demo
has four indexed draws/48 triangles and no per-frame mesh or texture uploads.

`Pipeline` specifies depth comparison/write, RGB/alpha blend factors/equations,
culling and color write masks. Each frame resets framebuffer/viewport/clear
state and invalidates the state cache. Each material supplies its own texture
usage and alpha cutoff, so those cannot leak from a previous draw.

`Frame` exclusively borrows `Renderer`: resource upload/allocation cannot
interleave draws and invalidate cached bindings. GPU handles have ownership and
release on drop, including partially failed resource creation. All Rust crates
forbid unsafe code. Mesh/texture bytes are copied during resource upload only.

The initial texture policy is straight RGBA, no implicit premultiplication or
color conversion, linear filtering and clamp-to-edge. Atlas, repeat sampling,
instancing, fog, lighting and full game material semantics belong to M2/M4.
This is not yet a port of the game's terrain or a finished material system.

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

114 pixel cases use independent numerical expected results. The browser test
also checks initialization, a rendered 3D frame, three aspect-ratio layouts and
absence of runtime errors/API requests. Results/screenshots are ignored outputs
under `build/rust/tests`. It uses software WebGL for reproducible CI; physical
GPU/browser performance is a later, separate measurement.

Production builds omit GPU probes. The C source, native program and previous
browser site are unchanged and remain available to differential tests.
