//! Query contracts vs the full native game, with focused overflow/ny==0 fixtures.
use std::{
    collections::BTreeSet,
    fs::File,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
use tankracer_assets::{archive::Archive, terrain::TerrainData};
use tankracer_game::terrain::{self, Error, Hit};

fn word(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
}
fn status(result: Result<bool, Error>) -> u32 {
    match result {
        Ok(false) => 0,
        Ok(true) => 1,
        Err(Error::Arithmetic) => (-2_i32) as u32,
        Err(Error::InvalidBucket) => (-1_i32) as u32,
    }
}
fn sentinel() -> Hit {
    Hit {
        height: -55,
        node: None,
        normal: [-12, -13, -14],
        surface: 0x8899aabb,
        special: -77,
        extra: [19, 23, -29, 31],
        flags: 0xabcd1234,
    }
}

fn compare(data: &TerrainData, points: &[[i32; 2]], args: &[&str], name: &str) {
    let executable =
        std::env::var_os("TANKRACER_TERRAIN_REFERENCE").expect("Set TANKRACER_TERRAIN_REFERENCE");
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/terrain");
    std::fs::create_dir_all(&root).unwrap();
    let input = root.join(format!("{name}.input"));
    let text: String = points
        .iter()
        .map(|p| format!("{} {}\n", p[0], p[1]))
        .collect();
    std::fs::write(&input, text).unwrap();
    let output = Command::new(executable)
        .args(args)
        .stdin(Stdio::from(File::open(input).unwrap()))
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{name}: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"TQ01");
    assert_eq!(output.stdout.len(), 8 + points.len() * 80);
    let base = word(&output.stdout, 4);
    let mut hits = 0;
    for (index, &[x, z]) in points.iter().enumerate() {
        let expected = &output.stdout[8 + index * 80..8 + (index + 1) * 80];
        let mut hit = sentinel();
        let mut h = 0x12345678;
        let mut nh = 0x12345678;
        let mut normal = [0x11111111, 0x22222222, 0x33333333];
        let actual = status(terrain::query(data, x, z, &mut hit));
        assert_eq!(
            actual,
            word(expected, 0),
            "{name} point{index} ({x},{z}) full status"
        );
        let node = hit
            .node
            .map_or(0x11223344, |id| base + data.node(id).source_offset);
        let values = [
            hit.height as u32,
            node,
            hit.normal[0] as u32,
            hit.normal[1] as u32,
            hit.normal[2] as u32,
            hit.surface,
            hit.special as u32,
            hit.extra[0] as u32,
            hit.extra[1] as u32,
            hit.extra[2] as u32,
            hit.extra[3] as u32,
            hit.flags,
        ];
        for (i, value) in values.iter().enumerate() {
            assert_eq!(
                *value,
                word(expected, 4 + i * 4),
                "{name} point{index} ({x},{z}) field{i}"
            );
        }
        assert_eq!(
            status(terrain::height(data, x, z, &mut h)),
            word(expected, 52),
            "{name} height status ({x},{z})"
        );
        assert_eq!(h as u32, word(expected, 56), "{name} height ({x},{z})");
        assert_eq!(
            status(terrain::normal_height(data, x, z, &mut nh, &mut normal)),
            word(expected, 60),
            "{name} normal-height status ({x},{z})"
        );
        assert_eq!(
            nh as u32,
            word(expected, 64),
            "{name} normal-height ({x},{z})"
        );
        for (i, &v) in normal.iter().enumerate() {
            assert_eq!(
                v as u32,
                word(expected, 68 + i * 4),
                "{name} normal-height normal ({x},{z})"
            );
        }
        hits += usize::from(actual == 1);
    }
    eprintln!(
        "{name}: {} points, {hits} hits, all three C query contracts exact; {}",
        points.len(),
        String::from_utf8_lossy(&output.stderr).trim()
    );
}

fn points(data: &TerrainData) -> Vec<[i32; 2]> {
    let mut points = BTreeSet::new();
    for x in [i32::MIN, -1, 0, 1, i32::MAX] {
        for z in [i32::MIN, -1, 0, 1, i32::MAX] {
            points.insert([x, z]);
        }
    }
    for mesh in &data.meshes {
        for triangle in &mesh.triangles {
            let p = triangle.indices.map(|i| mesh.vertices[i as usize]);
            let center = std::array::from_fn(|axis| {
                let component = if axis == 0 { 0 } else { 2 };
                mesh.position[component]
                    .wrapping_mul(8)
                    .wrapping_add((p.iter().map(|v| i32::from(v[component])).sum::<i32>() * 8) / 3)
            });
            points.insert(center);
            for v in p {
                let x = mesh.position[0]
                    .wrapping_add(i32::from(v[0]))
                    .wrapping_mul(8);
                let z = mesh.position[2]
                    .wrapping_add(i32::from(v[2]))
                    .wrapping_mul(8);
                for [dx, dz] in [[0, 0], [-1, 0], [1, 0], [0, -1], [0, 1], [7, 7], [-7, -7]] {
                    points.insert([x.wrapping_add(dx), z.wrapping_add(dz)]);
                }
            }
        }
    }
    for cell in 0..9 {
        for other in 0..8 {
            let wx =
                data.origin[0].wrapping_add(cell * (1_i32.wrapping_shl(u32::from(data.shifts[0]))));
            let wz =
                data.origin[1].wrapping_add(cell * (1_i32.wrapping_shl(u32::from(data.shifts[1]))));
            let ox = data.origin[0]
                .wrapping_add(other * (1_i32.wrapping_shl(u32::from(data.shifts[0]))));
            let oz = data.origin[1]
                .wrapping_add(other * (1_i32.wrapping_shl(u32::from(data.shifts[1]))));
            for offset in [-1, 0, 1, 7] {
                points.insert([wx.wrapping_mul(8).wrapping_add(offset), oz.wrapping_mul(8)]);
                points.insert([ox.wrapping_mul(8), wz.wrapping_mul(8).wrapping_add(offset)]);
            }
        }
    }
    points.into_iter().collect()
}

#[test]
#[ignore = "requires the native terrain oracle"]
fn all_maps_match_full_game_queries_at_faces_edges_and_grid_boundaries() {
    let assets = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    for map in [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ] {
        let bytes = std::fs::read(assets.join("LEVELS").join(map).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let name = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap()
            .name;
        let data = TerrainData::decode_bsm(archive.get(name).unwrap()).unwrap();
        compare(&data, &points(&data), &[assets.to_str().unwrap(), map], map);
    }
}

fn fixture(normal: [i16; 3], overflow: bool) -> Vec<u8> {
    let mut bytes = vec![0; 580];
    fn put16(b: &mut [u8], at: usize, v: i16) {
        b[at..at + 2].copy_from_slice(&v.to_le_bytes());
    }
    fn put32(b: &mut [u8], at: usize, v: u32) {
        b[at..at + 4].copy_from_slice(&v.to_le_bytes());
    }
    for (at, value) in [(0, 560), (4, 4), (8, 96), (16, 272)] {
        put32(&mut bytes, at, value);
    }
    let d = &mut bytes[20..];
    put16(d, 0, 3);
    put16(d, 2, 1);
    put32(d, 4, 48);
    put32(d, 8, 72);
    put16(d, 12, 1000);
    put32(d, 28, 0x10);
    put32(d, 40, 500);
    let vertices = if overflow {
        [[1, 3, 0], [-32768, 3, 0], [1, 3, -100]]
    } else {
        [[0, 3, 0], [100, 3, 0], [0, 3, 100]]
    };
    for (i, v) in vertices.iter().enumerate() {
        for (axis, &value) in v.iter().enumerate() {
            put16(d, 48 + i * 8 + axis * 2, value);
        }
    }
    for (i, &v) in normal.iter().enumerate() {
        put16(d, 72 + i * 2, v);
    }
    d[80..83].copy_from_slice(&[0, 1, 2]);
    put16(d, 90, 0x2100);
    put32(d, 96 + 12, 264);
    put16(d, 96 + 0x92, 32767);
    put16(d, 96 + 0x96, 32767);
    put16(d, 96 + 0x98, 77);
    for (i, v) in [11, -12, 13, -14].into_iter().enumerate() {
        put16(d, 96 + 0x9a + i * 2, v);
    }
    put32(d, 96 + 0xa4, 1);
    d[264..266].copy_from_slice(&[0, 255]);
    put32(d, 272, 552);
    put32(d, 272 + 0x108, (-32768_i32) as u32);
    put32(d, 272 + 0x10c, (-32768_i32) as u32);
    d[272 + 0x110] = 13;
    d[272 + 0x112] = 13;
    put16(d, 272 + 0x114, 2);
    put32(d, 552, 96);
    bytes
}

#[test]
#[ignore = "requires the native terrain oracle"]
fn ny_zero_division_overflow_and_partial_outputs_match_c() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/terrain");
    std::fs::create_dir_all(&root).unwrap();
    for (name, normal, overflow) in [
        ("flat", [0, 4096, 0], false),
        ("vertical", [4096, 0, 0], false),
        ("overflow", [8192, -1, 0], true),
    ] {
        let bytes = fixture(normal, overflow);
        let data = TerrainData::decode_bsm(&bytes).unwrap();
        let path = root.join(format!("{name}.bsm"));
        std::fs::write(&path, bytes).unwrap();
        let points = if overflow {
            vec![
                [-32767 * 8, 0],
                [-32767 * 8 - 1, 0],
                [0, 0],
                [999999, 999999],
            ]
        } else {
            vec![
                [8, 8],
                [0, 0],
                [800, 0],
                [801, 0],
                [-1, 0],
                [0, -1],
                [50, 50],
                [400, 400],
                [999999, 999999],
            ]
        };
        if name != "flat" {
            let point = points[0];
            let mut hit = sentinel();
            assert_eq!(
                terrain::query(&data, point[0], point[1], &mut hit),
                Err(Error::Arithmetic),
                "fixture must exercise its intended arithmetic branch"
            );
        }
        compare(&data, &points, &["--bsm", path.to_str().unwrap()], name);
    }
}
