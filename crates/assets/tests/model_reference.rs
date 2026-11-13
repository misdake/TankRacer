use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{
    archive::Archive,
    model::{Model, ShapeArchive},
};

struct Words<'a> {
    bytes: &'a [u8],
    at: usize,
}
impl Words<'_> {
    fn word(&mut self) -> u32 {
        let value = u32::from_le_bytes(self.bytes[self.at..self.at + 4].try_into().unwrap());
        self.at += 4;
        value
    }
    fn signed(&mut self) -> i32 {
        self.word() as i32
    }
}

#[test]
#[ignore = "requires C shape oracle; set TANKRACER_MODEL_REFERENCE"]
fn every_shipped_shape_matches_c_lookup_relocation_materials_and_geometry() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/models");
    fs::create_dir_all(&folder).unwrap();
    let mut models = 0;
    let mut vertices = 0;
    let mut faces = 0;
    for pack in [
        "DATA",
        "COMMON",
        "GUI",
        "INGAME",
        "TANKS/BRONZE",
        "TANKS/SILVER",
        "TANKS/GOLD",
        "LEVELS/ENGLAND",
        "LEVELS/MEXICO",
        "LEVELS/SWAMP",
        "LEVELS/EAST",
        "LEVELS/THEME",
        "LEVELS/BIO",
        "LEVELS/MONACO",
        "LEVELS/CIRCUIT",
    ] {
        let bytes = fs::read(root.join(pack).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        for file in archive
            .entries()
            .iter()
            .filter(|entry| entry.name.ends_with(".SS"))
        {
            let source = archive.get(file.name).unwrap();
            let shapes = ShapeArchive::parse(source).unwrap();
            let path = folder.join(file.name);
            fs::write(&path, source).unwrap();
            let output = Command::new(
                std::env::var_os("TANKRACER_MODEL_REFERENCE")
                    .expect("set TANKRACER_MODEL_REFERENCE"),
            )
            .arg(path)
            .output()
            .unwrap();
            assert!(
                output.status.success(),
                "{pack}/{} {}",
                file.name,
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(&output.stdout[..4], b"SM02");
            let mut out = Words {
                bytes: &output.stdout,
                at: 4,
            };
            assert_eq!(out.word() as usize, shapes.entries().len());
            for entry in shapes.entries() {
                let offset = out.word() as usize;
                let length = out.word() as usize;
                let raw = shapes.get(entry.name).unwrap();
                assert_eq!(
                    &source[offset..offset + length],
                    raw,
                    "{pack} {} SS lookup span",
                    entry.name
                );
                if length == 0 {
                    continue;
                }
                let model = Model::decode(raw).unwrap();
                let label = format!("{pack}/{} {}", file.name, entry.name);
                assert_eq!(
                    out.word() as usize,
                    model.geometry.vertices.len(),
                    "{label}"
                );
                assert_eq!(
                    out.word() as usize,
                    model.geometry.triangles.len(),
                    "{label}"
                );
                assert_eq!(out.word(), u32::from(model.radius), "{label}");
                assert_eq!(out.word(), u32::from(model.render_hint), "{label}");
                for &v in model.bounds.iter().flatten() {
                    assert_eq!(out.signed(), i32::from(v), "{label} bounds");
                }
                for &v in model.geometry.vertices.iter().flatten() {
                    assert_eq!(out.signed(), i32::from(v), "{label} vertices");
                }
                assert_eq!(
                    out.word(),
                    u32::from(model.vertex_normals.is_some()),
                    "{label} layer normals"
                );
                if let Some(normals) = &model.vertex_normals {
                    for &v in normals.iter().flatten() {
                        assert_eq!(out.signed(), i32::from(v), "{label} layer normal");
                    }
                }
                for column in &model.face_centers {
                    assert_eq!(
                        out.word(),
                        u32::from(column.is_some()),
                        "{label} center column"
                    );
                    if let Some(column) = column {
                        for &value in column {
                            assert_eq!(out.signed(), i32::from(value), "{label} center");
                        }
                    }
                }
                for (index, tri) in model.geometry.triangles.iter().enumerate() {
                    let mut expected = tri.normal.map(|v| v as u32).to_vec();
                    expected.push(u32::from(tri.color));
                    expected.extend(tri.indices.map(u32::from));
                    expected.push(u32::from(tri.sides));
                    expected.extend(tri.uv.into_iter().flatten().map(u32::from));
                    expected.extend([u32::from(tri.flags), tri.material_source_offset]);
                    let actual: Vec<_> = (0..16).map(|_| out.word()).collect();
                    assert_eq!(actual, expected, "{label} triangle {index}");
                    let mut name = Vec::new();
                    for _ in 0..4 {
                        name.extend(out.word().to_le_bytes());
                    }
                    let end = name.iter().position(|&v| v == 0).unwrap();
                    let name = std::str::from_utf8(&name[..end]).unwrap();
                    if let Some(id) = tri.material {
                        let material = &model.materials[id.index()];
                        assert_eq!(material.name, name.to_ascii_lowercase(), "{label} material");
                        assert!(material.spellings.iter().any(|s| s == name));
                    } else {
                        assert!(name.is_empty(), "{label} material should be absent");
                    }
                    if let Some(billboard) = &tri.billboard {
                        assert_eq!(
                            [out.signed(), out.signed()],
                            billboard.center.map(i32::from),
                            "{label} billboard"
                        );
                    }
                }
                models += 1;
                vertices += model.geometry.vertices.len();
                faces += model.geometry.triangles.len();
            }
            assert_eq!(
                out.at,
                out.bytes.len(),
                "{pack}/{} trailing words",
                file.name
            );
        }
    }
    eprintln!(
        "{models} shapes, {vertices} vertices and {faces} triangles exactly match C, including names/normals/bounds/face centers"
    );
}
