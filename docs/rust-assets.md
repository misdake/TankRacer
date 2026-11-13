# Rust resource import

## BSM geometry checkpoint

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

## SS bundles and SM models

`ShapeArchive` reads the original SS directory as borrowed, checked payload
spans. Names match ASCII case-insensitively, with optional `.SM` suffix; empty
entries remain distinguishable from missing resources. `Model` owns typed SM
geometry, material identities, bounds, radius, render hint and optional vertex
normals. SM and BSM share one checked geometry decoder; neither needs original
addresses, relocation or a guest-memory arena.

The original SM octant face-order lists are not retained: the new renderer uses
world-space geometry and depth testing. Triangle order, material/blend inputs,
UVs, colors, normals and billboard data are preserved. Model loading does not
create GPU resources or select a rendering approximation.

Across all fifteen archives, **435 nonempty models, 11,713 vertices and 10,430
triangles** match the qualified C SS lookup/shape relocation, including source
spans, bounds, normals and material names. The shared decoder refactor also
passes all 80,439 existing BSM triangle comparisons and the browser's 133 GPU
pixel cases; the ENGLAND scene remains five draws. Malformed directory/range
and geometry checks remain ordinary native tests.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/reference_model.c build/native/libtankracer_core.a -o build/native/model_reference.exe
$env:TANKRACER_MODEL_REFERENCE=(Resolve-Path build/native/model_reference.exe).Path
cargo test -p tankracer-assets --test model_reference -- --ignored --nocapture
```

Vehicle model assembly and instance ownership are documented in
[rust-physics.md](rust-physics.md). These are original resource data and
per-vehicle state, not additional per-map acceleration structures.


## Authored level object records (FSTPC 5)

`level::Level::decode` reads the common record format from actual archived map
files: class/name, position, authored angles, shape name and nested parameter
text. Names and parameter groups borrow their archive bytes, avoiding per-field
string copies or a guest-memory representation. The object vector is decoded
resource metadata, not a new spatial acceleration table. The Paths payload is
retained uninterpreted; class construction and reference resolution are separate
pending operations.

Body-token delimiters/capacities follow `frame_world_text`: space/CR/LF only,
with full token consumption before bounded string truncation. Parenthesized
groups retain nesting and source whitespace, with the original 1,023-byte bound.
Coordinates use the source decimal numeric prefix and binary64 -> signed64 low32
conversion after multiplying by 1,024; angles use wrapping integer * 182.
`Pose::from_level` in the game crate then initializes identity and rotates Y/X/Z
sequentially using the exact Q12 rules. It deliberately does not use the combined
angle formula, which has different intermediate rounding.

**All 50 shipped FSTPC 5 files / 4,392 records across all eight CUP map archives
match the actual C common record reader**: class/name/shape, positions, all nine
matrix words and nested parameter bytes. The test invokes
`frame_world_object_read` rather than copying a conversion/matrix formula into
the oracle. Class-specific parent/final slots are explicitly empty and shape
loading is bypassed after comparing the name; this test does **not** qualify
class constructors, model resolution, entity allocation, named forward references
or the final initialized world. Those remain the next integration work.

Ordinary workspace tests also decode all 50 files and verify that string/group
slices remain inside their source. Focused cases cover negative/overflowing
integers, decimal prefixes, D exponents, truncated shape names, nested groups,
missing Paths, invalid version and oversized groups. Legacy formats, arbitrary
file-header comments/quoting and malformed-memory prefix parity are not claimed.
Native/all-target and Wasm game Clippy pass; all 244 production C files remain
unchanged. The renderer/browser is unchanged.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ihost tests/reference_level.c build/native/libtankracer_core.a -lm -o build/native/level_reference.exe
$env:TANKRACER_LEVEL_REFERENCE=(Resolve-Path build/native/level_reference.exe).Path
cargo test -p tankracer-game --test level_reference -- --ignored --nocapture
```


Class-specific parameter reads now use `parameters::Parameters`, separate from
the outer file/config lexer: exact case/first match, spaces/tabs, parentheses,
quoted-token rules, wrapping integer and first-byte boolean conversion, and the
native final-token `more` condition. It borrows parameter text without an AST or
string copies. The actual shipped Crush constructor comparisons exercise it;
focused lexer tests also cover CR/LF attachment, repeated/case-distinct keys,
quoted names and an unreadable final integer. Other Obstacle constructors remain
pending; see the latest [physics checkpoint](rust-physics.md).
