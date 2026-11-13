//! Optional integration against the actual composed C loader, not a second BSM parser.
use std::{
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{archive::Archive, world::World};

fn u32_at(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
}
fn i16_at(bytes: &[u8], at: usize) -> i16 {
    i16::from_le_bytes(bytes[at..at + 2].try_into().unwrap())
}

#[test]
#[ignore = "requires the C reference executable; set TANKRACER_REFERENCE"]
fn static_geometry_matches_composed_c_loader_on_all_maps() {
    let executable = std::env::var_os("TANKRACER_REFERENCE")
        .expect("Set TANKRACER_REFERENCE to the built native host");
    let assets = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let mut checked = 0;
    for (map, cup) in [
        ("ENGLAND", "B"),
        ("MEXICO", "B"),
        ("SWAMP", "B"),
        ("EAST", "S"),
        ("THEME", "B"),
        ("BIO", "S"),
        ("MONACO", "S"),
        ("CIRCUIT", "G"),
    ] {
        let source = std::fs::read(assets.join("LEVELS").join(map).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&source).unwrap();
        let name = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap()
            .name;
        let world = World::decode(archive.get(name).unwrap()).unwrap();
        let mut child = Command::new(&executable)
            .args([
                "--assets",
                assets.to_str().unwrap(),
                "--map",
                map,
                "--cup",
                cup,
                "--snapshot",
            ])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        child.stdin.take().unwrap().write_all(b"0 0 1\n").unwrap();
        let output = child.wait_with_output().unwrap();
        assert!(
            output.status.success(),
            "{map}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        let packet = u32_at(&output.stdout, 0) as usize;
        assert_eq!(u32_at(&output.stdout, 4), 0x800000);
        let ram = &output.stdout[8 + packet..];
        assert_eq!(ram.len(), 0x800000);
        let base = 0x5dab54;
        for mesh in &world.meshes {
            let at = base + mesh.source_offset as usize;
            assert_eq!(usize::from(i16_at(ram, at) as u16), mesh.vertices.len());
            assert_eq!(
                usize::from(i16_at(ram, at + 2) as u16),
                mesh.triangles.len()
            );
            assert_eq!(u32_at(ram, at + 28), mesh.flags);
            for i in 0..3 {
                assert_eq!(u32_at(ram, at + 36 + i * 4) as i32, mesh.position[i]);
            }
            let vertices = u32_at(ram, at + 4) as usize;
            for (i, v) in mesh.vertices.iter().enumerate() {
                for (axis, value) in v.iter().enumerate() {
                    assert_eq!(
                        i16_at(ram, vertices + i * 8 + axis * 2),
                        *value,
                        "{map} vertex"
                    );
                }
            }
            for triangle in &mesh.triangles {
                let at = base + triangle.source_offset as usize;
                for i in 0..3 {
                    assert_eq!(i16_at(ram, at + i * 2), triangle.normal[i]);
                }
                assert_eq!(i16_at(ram, at + 6) as u16, triangle.color);
                assert_eq!(&ram[at + 8..at + 11], &triangle.indices);
                assert_eq!(ram[at + 11], triangle.sides);
                for i in 0..3 {
                    assert_eq!(&ram[at + 12 + i * 2..at + 14 + i * 2], &triangle.uv[i]);
                }
                assert_eq!(i16_at(ram, at + 18) as u16, triangle.flags);
                let material = u32_at(ram, at + 20) as usize;
                if let Some(id) = triangle.material {
                    let name = &world.materials[id.index()].name;
                    assert_ne!(material, 0, "{map}: missing material {name}");
                    let bytes = &ram[material..material + 16];
                    let end = bytes.iter().position(|&b| b == 0).unwrap();
                    // 501290 aliases ASCII case variants to the first cached
                    // spelling. Preserve source spelling, compare semantic identity.
                    assert!(
                        bytes[..end].eq_ignore_ascii_case(name.as_bytes()),
                        "{map} material identity: {:?} / {name}",
                        &bytes[..end]
                    );
                } else {
                    assert_eq!(material, 0);
                }
                checked += 1;
            }
        }
    }
    eprintln!(
        "C reference: {checked} triangles across all eight maps match Rust geometry/materials"
    );
}
