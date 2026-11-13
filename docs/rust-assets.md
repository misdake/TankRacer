# M2 resource import: BSM geometry checkpoint

`tankracer-assets` reads FASTF101 directories and BSM v4 geometry directly from
the shipped immutable archives. Source offsets remain provenance; the imported
world owns typed meshes, integer positions, vertices, triangle normals/colors,
indices, UV bytes, flags and interned material IDs. No loader relocation or 8 MiB guest
address space is needed by this Rust decoder. Collision graph offsets are
retained but collision decoding belongs to M3.

The implementation follows the qualified C shape relocation and presentation
field interpretation. A per-world table stores canonical material identities
and original spellings; triangles carry IDs/source offsets rather than repeated
heap strings. Identity is ASCII case-insensitive, matching the original cache.
Billboard/disabled/special flags remain data, not silently discarded content.

Inspect a real world without loading a game:

```sh
cargo run -p tankracer-assets --bin inspect-world -- assets/game/LEVELS/ENGLAND/FASTFILE.DAT
```

Normal native tests parse all eight CUP worlds and reject malformed ranges,
cycles and invalid vertex indices. An optional integration test launches the
existing C native host at frame zero, obtains its fully initialized data and
compares every imported vertex, origin, triangle normal/color/index/UV/flag and
resolved material identity. All 80,439 triangles across the eight maps match.

```powershell
$env:TANKRACER_REFERENCE=(Resolve-Path build/native/tankracer.exe).Path
cargo test -p tankracer-assets --test world_reference -- --ignored --nocapture
```

## SUR texture checkpoint

The Rust decoder covers the shipped graphics-device format selection, indexed8
and planar BGRA32, raw/whole-plane/row PCX compression and mip selection. It
reproduces RGB565/ARGB4444 conversion, color key, partial alpha and keyed opaque
black. It leaves filtering/atlas edge treatment to the renderer import stage.

The C oracle calls the existing qualified surface functions, not a reimplementation.
Across the 15 supplied archives, 1,007 decoded images/mips and 5,931,008 RGBA
pixels match exactly, including metadata and consumed input length. The one
invalid BIO resource is rejected by both implementations, not replaced by white.

```sh
gcc -std=c11 -O2 -Isrc tests/reference_surface.c build/native/libtankracer_core.a -o build/native/surface_reference
```

```powershell
$env:TANKRACER_SURFACE_REFERENCE=(Resolve-Path build/native/surface_reference.exe).Path
cargo test -p tankracer-assets --test surface_reference -- --ignored --nocapture
```

The decoded BSM/SUR data now feeds the actual ENGLAND static scene, material
classification and atlas described in `rust-renderer.md`. Placed scenery and
optional-module lifecycle remain subsequent work.
