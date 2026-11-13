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

This is **M2's first increment**, not a completed scene importer. Next: establish
the single coordinate/unit convention, decode SUR textures against C, classify
materials/samplers and connect actual ENGLAND geometry to the Rust renderer.
Placed scenery, optional-module ownership, atlases and static batching follow.
