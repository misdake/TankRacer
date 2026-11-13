use std::{
    collections::BTreeSet,
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{archive::Archive, terrain::TerrainData};
use tankracer_game::geometry;

fn compare(data: &TerrainData, points: &BTreeSet<[i32; 3]>, args: &[&str], name: &str) {
    let mut input = String::new();
    let mut expected = Vec::new();
    let mut hits = [0; 4];
    for &world in points {
        for (i, mode) in [0, 1, 2, -1].into_iter().enumerate() {
            input.push_str(&format!("{} {} {} {mode}\n", world[0], world[1], world[2]));
            let mut normal = [0x1234, -6, 89];
            let found = geometry::fallback(data, world, mode, &mut normal);
            hits[i] += usize::from(found);
            expected.push([
                u32::from(found),
                normal[0] as u32,
                normal[1] as u32,
                normal[2] as u32,
            ]);
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/terrain");
    fs::create_dir_all(&folder).unwrap();
    let file = folder.join(format!("{name}.fallback.input"));
    fs::write(&file, input).unwrap();
    let output = Command::new(std::env::var_os("TANKRACER_TERRAIN_REFERENCE").unwrap())
        .args(args)
        .arg("--fallback")
        .stdin(Stdio::from(fs::File::open(file).unwrap()))
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"FQ01");
    assert_eq!(output.stdout.len(), 8 + expected.len() * 16);
    for (index, values) in expected.iter().enumerate() {
        let actual: Vec<_> = output.stdout[8 + index * 16..8 + (index + 1) * 16]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(
            actual,
            values,
            "{name} fallback record{index} point={:?} mode={}",
            points.iter().nth(index / 4).unwrap(),
            [0, 1, 2, -1][index % 4]
        );
    }
    assert!(hits[0] > 0, "must exercise successful fallback selection");
    assert_eq!(
        hits[0], hits[3],
        "unknown mode must behave like unfiltered mode"
    );
    eprintln!(
        "{name}: {} fallback queries exact; mode hits={hits:?}",
        expected.len()
    );
}

#[test]
#[ignore = "requires the native terrain oracle"]
fn all_maps_fallback_centers_bounds_filters_and_far_points_match_c() {
    let assets = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    for map in [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ] {
        let bytes = fs::read(assets.join("LEVELS").join(map).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let file = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap();
        let data = TerrainData::decode_bsm(archive.get(file.name).unwrap()).unwrap();
        let mut points = BTreeSet::new();
        for node in &data.fallback.nodes {
            let mesh = &data.meshes[node.mesh as usize];
            for center in &mesh.face_centers {
                let world: [i32; 3] = std::array::from_fn(|i| {
                    mesh.position[i]
                        .wrapping_add(i32::from(center[i]))
                        .wrapping_mul(8)
                });
                for delta in [
                    [0, 0, 0],
                    [1, 0, 0],
                    [-1, 0, 0],
                    [0, 1, 0],
                    [0, -1, 0],
                    [0, 0, 1],
                    [0, 0, -1],
                    [0, 2400000, 0],
                    [0, -2400000, 0],
                ] {
                    points.insert(std::array::from_fn(|i| world[i].wrapping_add(delta[i])));
                }
            }
            for dx in [-1, 0, 1] {
                for dz in [-1, 0, 1] {
                    points.insert([
                        mesh.position[0]
                            .wrapping_add(i32::from(mesh.radius) * 2 + dx)
                            .wrapping_mul(8),
                        mesh.position[1].wrapping_mul(8),
                        mesh.position[2]
                            .wrapping_add(i32::from(mesh.radius) * 2 + dz)
                            .wrapping_mul(8),
                    ]);
                }
            }
        }
        for x in [i32::MIN, 0, i32::MAX] {
            for y in [i32::MIN, 0, i32::MAX] {
                for z in [i32::MIN, 0, i32::MAX] {
                    points.insert([x, y, z]);
                }
            }
        }
        compare(&data, &points, &[assets.to_str().unwrap(), map], map);
    }
}

fn fixture(flag: u32, reverse: bool, y: i32) -> Vec<u8> {
    fn word(b: &mut [u8], at: usize, v: u32) {
        b[at..at + 4].copy_from_slice(&v.to_le_bytes());
    }
    fn half(b: &mut [u8], at: usize, v: i16) {
        b[at..at + 2].copy_from_slice(&v.to_le_bytes());
    }
    let mut bytes = vec![0; 504];
    for (at, v) in [(0, 484), (4, 4), (12, 136), (16, 192)] {
        word(&mut bytes, at, v);
    }
    let d = &mut bytes[20..];
    half(d, 0, 3);
    half(d, 2, 2);
    for (at, v) in [
        (4, 48),
        (8, 72),
        (16, 120),
        (20, 124),
        (24, 128),
        (28, flag),
        (36, 1000),
        (40, y as u32),
        (44, 2000),
        (144, 472),
    ] {
        word(d, at, v);
    }
    half(d, 12, 100);
    for (i, n) in [[111, 222, 333], [444, 555, 666]].iter().enumerate() {
        for (axis, &v) in n.iter().enumerate() {
            half(d, 72 + i * 24 + axis * 2, v);
        }
    }
    for (i, v) in [4096, 0, 0, 4096, -100, 100, -100, 100]
        .into_iter()
        .enumerate()
    {
        half(d, 148 + i * 2, v);
    }
    word(d, 196, 476);
    for cell in 0..64 {
        half(d, 192 + 8 + cell * 2, -1);
    }
    word(d, 192 + 0x108, (-32768_i32) as u32);
    word(d, 192 + 0x10c, (-32768_i32) as u32);
    d[192 + 0x110] = 13;
    d[192 + 0x112] = 13;
    half(d, 192 + 0x116, 2);
    d[472..475].copy_from_slice(if reverse { &[1, 0, 255] } else { &[0, 1, 255] });
    word(d, 476, 136);
    bytes
}

#[test]
#[ignore = "requires the native terrain oracle"]
fn stable_ties_mode_two_cutoff_and_unsigned_magnitudes_match_c() {
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/terrain");
    fs::create_dir_all(&folder).unwrap();
    for (name, flag, reverse, y) in [
        ("plain", 0, false, 500),
        ("reversed", 0, true, 500),
        ("flag4", 4, false, 500),
        ("wrapped", 0, false, i32::MIN),
    ] {
        let bytes = fixture(flag, reverse, y);
        let file = folder.join(format!("fallback-{name}.bsm"));
        fs::write(&file, &bytes).unwrap();
        let data = TerrainData::decode_bsm(&bytes).unwrap();
        let mut points = BTreeSet::new();
        for x in [-101_i32, -100, -1, 0, 1, 100, 101] {
            for z in [-101_i32, -100, 0, 100, 101] {
                for dy in [-300000_i32, -299999, 0, 299999, 300000] {
                    points.insert([
                        (1000 + x) * 8,
                        y.wrapping_add(dy).wrapping_mul(8),
                        (2000 + z) * 8,
                    ]);
                }
            }
        }
        let center = [8000, if name == "wrapped" { 0 } else { 4000 }, 16000];
        let mut normal = [0; 3];
        assert!(geometry::fallback(
            &data,
            center,
            if flag == 4 { 2 } else { 0 },
            &mut normal
        ));
        assert_eq!(
            normal,
            if reverse {
                [444, 555, 666]
            } else {
                [111, 222, 333]
            }
        );
        points.insert(center);
        compare(&data, &points, &["--bsm", file.to_str().unwrap()], name);
    }
}
