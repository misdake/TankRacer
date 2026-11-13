use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{
    archive::Archive,
    config::{ResolveContext, ResolvedTank, Surfaces, Tank},
};

fn encode(value: &ResolvedTank) -> Vec<u32> {
    let tank = &value.tank;
    let d = &tank.drive;
    let s = &tank.suspension;
    let mut out = Vec::new();
    for node in &tank.nodes {
        let mut bytes = [0; 16];
        bytes[..node.name.len()].copy_from_slice(node.name.as_bytes());
        out.extend(
            bytes
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
        );
        out.extend(node.offset.map(|v| v as u32));
    }
    out.extend(tank.glow_offset.map(|v| v as u32));
    out.extend(
        [
            d.max_rpm,
            d.max_power,
            d.max_turn,
            d.initial_power,
            d.power_mul,
            d.max_brakes,
        ]
        .map(|v| v as u32),
    );
    out.extend(
        [
            d.turn_fraction,
            d.hill_roll,
            d.understeer_fraction,
            d.rearweight_fraction,
            d.rearweight_accel_fraction,
        ]
        .map(|g| g.packed()),
    );
    out.extend([
        d.traction_limit as u32,
        d.catchup_traction_limit as u32,
        s.initial_weight as u32,
        s.initial_pressure as u32,
        s.pressure_fraction.packed(),
        d.sidegrip_fraction.packed(),
        s.cog_offset as u32,
    ]);
    out.extend(
        [
            d.forward_drag,
            d.track_drag,
            d.track_air_drag,
            d.track_spin_drag,
            d.gravity_drag,
            d.rotational_drag,
            d.air_rotational_drag,
            d.sideways_drag,
            d.force_drag,
        ]
        .map(|g| g.packed()),
    );
    out.extend([
        s.force_to_susp.packed(),
        s.down_force_to_susp.packed(),
        s.susp_to_force.packed(),
        s.susp_to_rot.packed(),
        s.spring_pre_shift as u32,
        s.spring.packed(),
        s.easy_spring.packed(),
        s.susp_speed_drag.packed(),
        s.susp_drag.packed(),
        s.accel_to_rot.packed(),
        s.steer_to_rot.packed(),
        s.susp_to_shape.packed(),
        s.shape_min as u32,
        s.shape_max as u32,
        d.turbo_amount as u32,
        d.turbo_power_mul as u32,
    ]);
    out.extend(tank.turbo_offset.map(|v| v as u32));
    assert_eq!(out.len(), 119);
    out.extend(s.travel_bounds().unwrap().map(|v| v as u32));
    out.push(d.sidegrip_fraction.packed());
    out.extend(value.mode5_backup.unwrap_or([0; 2]).map(|v| v as u32));
    out
}
fn run(bytes: &[u8], name: &str, surface: bool, contexts: &[ResolveContext]) -> Vec<Vec<u32>> {
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/config");
    fs::create_dir_all(&folder).unwrap();
    let file = folder.join(name);
    fs::write(&file, bytes).unwrap();
    let mut input = Vec::new();
    for c in contexts {
        let words = if surface {
            vec![0]
        } else {
            vec![
                u32::from(c.special),
                u32::from(c.player),
                c.mode,
                c.map,
                c.circuit_power_mul as u32,
                u32::from(c.ambient_battle.is_some()),
                c.ambient_battle.unwrap_or([0; 2])[0] as u32,
                c.ambient_battle.unwrap_or([0; 2])[1] as u32,
            ]
        };
        input.extend(words.into_iter().flat_map(u32::to_le_bytes));
    }
    let request = folder.join(format!("{name}.input"));
    fs::write(&request, input).unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_CONFIG_REFERENCE").expect("set TANKRACER_CONFIG_REFERENCE"),
    )
    .arg(if surface { "--surface" } else { "--tank" })
    .arg(file)
    .stdin(Stdio::from(fs::File::open(request).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"CF01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let size = if surface { 102 } else { 126 };
    assert_eq!(words.len(), size * contexts.len());
    words
        .chunks_exact(size)
        .map(|record| {
            assert_eq!(record[0], 0, "{name}");
            assert_eq!(record[1] as usize, bytes.len(), "{name} consumed");
            record[2..].to_vec()
        })
        .collect()
}

#[test]
#[ignore = "requires the C config oracle; set TANKRACER_CONFIG_REFERENCE"]
fn all_authored_resources_and_post_load_overrides_match_c() {
    let bytes = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game/DATA/FASTFILE.DAT"),
    )
    .unwrap();
    let archive = Archive::parse(&bytes).unwrap();
    let source = archive.get("SURFS.DAT").unwrap();
    let surfaces = Surfaces::decode(source).unwrap();
    let mut expected = Vec::new();
    for surface in surfaces.entries {
        expected.extend([surface.drag, surface.grip].map(|g| g.packed()));
        expected.extend(surface.slide.map(|g| g.packed()));
        expected.push(surface.bump.packed());
    }
    assert_eq!(
        run(source, "SURFS.DAT", true, &[ResolveContext::default()])[0],
        expected
    );
    let mut contexts = Vec::new();
    for special in [false, true] {
        for player in [false, true] {
            for mode in [0, 1, 2, 5] {
                for map in [0, 7] {
                    contexts.push(ResolveContext {
                        special,
                        player,
                        mode,
                        map,
                        circuit_power_mul: 777,
                        ambient_battle: Some([666, 888]),
                    });
                }
            }
        }
    }
    for i in 1..=15 {
        let name = format!("TANK{i}.DAT");
        let source = archive.get(&name).unwrap();
        let tank = Tank::decode(source).unwrap();
        let actual = run(source, &name, false, &contexts);
        for (context, words) in contexts.iter().zip(actual) {
            assert_eq!(
                words,
                encode(&tank.resolve(*context).unwrap()),
                "{name} {context:?}"
            );
        }
    }
    let fixture=b"# widths and duplicate policy\nMAX_TURN: 1\nmax_turn: 4294967297\nmax_turn_battle: -13\ntraction_limit_battle: 25\nmax_power: 2147483647\nsidegrip_fraction: -1 65535\nforward_drag: 65535 257 258\nbody_node: \"tank b.sm\" -1 2 3\n";
    let tank = Tank::decode(fixture).unwrap();
    for (context, words) in contexts
        .iter()
        .zip(run(fixture, "widths.DAT", false, &contexts))
    {
        assert_eq!(
            words,
            encode(&tank.resolve(*context).unwrap()),
            "width fixture {context:?}"
        );
    }
    eprintln!(
        "15 authored tanks + width fixture × 32 override contexts and all 100 surface factors exactly match C"
    );
}
