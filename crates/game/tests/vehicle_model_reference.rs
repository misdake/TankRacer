use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{
    archive::Archive,
    config::{Part, ResolveContext, Surfaces, Tank},
    model::ShapeArchive,
};
use tankracer_game::vehicle_model::{Bounds, BuildContext, Bundles, ModelBank, VehicleModel};

fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn bounds(out: &mut Vec<u32>, value: Bounds) {
    append(out, value.min);
    append(out, value.max);
}
fn encode(v: &VehicleModel) -> Vec<u32> {
    let mut out = Vec::new();
    for point in v.primary_local {
        append(&mut out, point);
    }
    for point in v.auxiliary_local {
        append(&mut out, point);
    }
    append(&mut out, v.track_dimensions);
    bounds(&mut out, v.geometry_bounds);
    bounds(&mut out, v.collision_bounds);
    for value in v.query_bounds {
        bounds(&mut out, value);
    }
    out.push(v.aerial_height as u32);
    out.extend(v.gun_vertices.map(u32::from));
    append(&mut out, v.gun_original_z.map(i32::from));
    out.extend(v.damage_vertices.into_iter().flatten().map(u32::from));
    out.push(u32::from(
        v.parts[Part::Gun.slot()].as_ref().unwrap().radius,
    ));
    for vertex in &v.shadow.geometry().vertices[..4] {
        append(&mut out, vertex.map(i32::from));
    }
    out.extend(
        v.shadow.geometry().triangles[..2]
            .iter()
            .map(|t| u32::from(t.color)),
    );
    append(&mut out, v.alternate_control.map(i32::from));
    for part in [
        Part::Body,
        Part::Turret,
        Part::Gun,
        Part::LeftTrack,
        Part::RightTrack,
        Part::Under,
        Part::End,
        Part::Aerial,
        Part::Bottom,
    ] {
        append(&mut out, v.offsets[part.slot()]);
    }
    out.extend([
        v.config.tank.drive.sidegrip_fraction.packed(),
        v.reset_sidegrip.packed(),
        v.config.tank.drive.max_turn as u32,
        v.config.tank.drive.rotational_drag.packed(),
    ]);
    for track in &v.tracks {
        for group in &track.groups {
            out.push(group.len() as u32);
            for reference in group {
                out.extend([
                    u32::from(reference.triangle),
                    u32::from(reference.corner),
                    u32::from(reference.snapped_v),
                ]);
            }
        }
    }
    out
}
fn shapes(bytes: &[u8]) -> ShapeArchive<'_> {
    let archive = Archive::parse(bytes).unwrap();
    let entry = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".SS"))
        .unwrap();
    ShapeArchive::parse(archive.get(entry.name).unwrap()).unwrap()
}

#[test]
#[ignore = "requires full native model startup oracle; set TANKRACER_VEHICLE_MODEL_REFERENCE"]
fn all_fifteen_assembled_models_match_native_game_contacts_bounds_uvs_and_copies() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let vehicle_files: Vec<_> = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| fs::read(root.join("TANKS").join(tier).join("FASTFILE.DAT")).unwrap())
        .into_iter()
        .collect();
    let shared_file = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let source = Bundles {
        vehicle: vehicle_files.iter().map(|b| shapes(b)).collect(),
        shared: vec![shapes(&shared_file)],
        level: Vec::new(),
    };
    let data = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let archive = Archive::parse(&data).unwrap();
    let surfaces = Surfaces::decode(archive.get("SURFS.DAT").unwrap()).unwrap();
    let mut bank = ModelBank::default();
    let mut profiles = 0;
    for vehicle in 1..=15 {
        let tank = Tank::decode(archive.get(&format!("TANK{vehicle}.DAT")).unwrap()).unwrap();
        for map in [0, 7] {
            for mode in [0, 1] {
                let output = Command::new(
                    std::env::var_os("TANKRACER_VEHICLE_MODEL_REFERENCE")
                        .expect("set TANKRACER_VEHICLE_MODEL_REFERENCE"),
                )
                .arg(&root)
                .arg(vehicle.to_string())
                .arg(map.to_string())
                .arg(mode.to_string())
                .output()
                .unwrap();
                assert!(
                    output.status.success(),
                    "vehicle={vehicle} map={map} mode={mode}: {}",
                    String::from_utf8_lossy(&output.stderr)
                );
                assert_eq!(&output.stdout[..4], b"VM01");
                let words: Vec<_> = output.stdout[4..]
                    .chunks_exact(4)
                    .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                    .collect();
                assert_eq!(words[0], 1);
                assert_eq!(words[1], mode);
                assert_eq!(words[2], map);
                let context = BuildContext {
                    resolve: ResolveContext {
                        player: words[0] != 0,
                        mode: words[1],
                        map: words[2],
                        special: words[3] != 0,
                        circuit_power_mul: words[6] as i32,
                        ambient_battle: None,
                    },
                    ambient_wobble: words[4] != 0,
                    session_active: words[5] != 0,
                };
                let model =
                    VehicleModel::build(&tank, &source, &mut bank, &surfaces, context).unwrap();
                let expected = encode(&model);
                assert_eq!(
                    words[7..],
                    expected,
                    "vehicle={vehicle} map={map} mode={mode}"
                );
                for part in [Part::Body, Part::Turret, Part::Gun] {
                    assert!(model.parts[part.slot()].as_ref().unwrap().is_private());
                }
                for part in [Part::LeftTrack, Part::RightTrack] {
                    assert!(!model.parts[part.slot()].as_ref().unwrap().is_private());
                }
                let gun = model.parts[Part::Gun.slot()].as_ref().unwrap();
                assert_eq!(gun.radius, gun.source.radius.wrapping_mul(2));
                profiles += 1;
            }
        }
    }
    eprintln!(
        "{profiles} full native initializations match model-derived contacts, bounds, offsets, shadow and track UV tables"
    );
}
